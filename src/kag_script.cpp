#include "kag_script.h"

#include "kag_host.h"
#include "KAGParser.h"
#include "tjs_script_internal.h"

#include <Windows.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

// KagScript / BasicKagScript reconstruction (docs/kag-reconstruction.md). Every behaviour below that was not read from
// pseudocode or confirmed against the original DLL is marked UNVERIFIED.
namespace {

using cm3d2::kag::HostScope;
using cm3d2::kag::hashUtf16;

std::atomic<std::uint64_t> g_next_unique_id{0};  // DAT_18021de58

std::wstring fromUtf8(const char* text) {
    if (text == nullptr || text[0] == 0) return {};
    const int length = static_cast<int>(std::strlen(text));
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, length, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text, length, out.data(), size);
    return out;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string toUtf8(const ttstr& text) { return toUtf8(std::wstring(text.c_str())); }
ttstr toTtstr(const std::wstring& text) { return ttstr(text.c_str()); }

// Collects member names and values (the DLL's Dictionary.getKeyArray, FUN_18006fbf0). The enumeration only continues
// while the callback result is true.
class MemberCollector final : public TJS::tTJSDispatch {
public:
    std::vector<std::pair<ttstr, tTJSVariant>> items;
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant* result, tjs_int numparams,
        tTJSVariant** param, iTJSDispatch2*) override {
        if (numparams >= 1) items.emplace_back(ttstr(*param[0]), numparams >= 3 ? *param[2] : tTJSVariant());
        if (result != nullptr) *result = tTJSVariant(true);
        return TJS_S_OK;
    }
};

// Strings in the DLL's struct text escape only the double quote and the backslash (confirmed against the original).
ttstr quoteStructString(const ttstr& text) {
    std::wstring out = L"\"";
    for (const tjs_char* p = text.c_str(); *p != 0; ++p) {
        if (*p == L'"' || *p == L'\\') out.push_back(L'\\');
        out.push_back(*p);
    }
    out.push_back(L'"');
    return ttstr(out.c_str());
}

// The DLL's Dictionary.toStructString: compact "(const)%[ "key",value,... ]" for dictionaries and "(const)[ ... ]"
// for arrays; other values use the engine's expression string.
ttstr structString(const tTJSVariant& value) {
    if (value.Type() == TJS::tvtObject) {
        iTJSDispatch2* object = value.AsObjectNoAddRef();
        if (object == nullptr) return ttstr(TJS_W("null"));
        if (object->IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"), object) == TJS_S_TRUE) {
            MemberCollector collector;
            tTJSVariantClosure closure(&collector, nullptr);
            object->EnumMembers(TJS_IGNOREPROP, &closure, object);
            ttstr out(TJS_W("(const)%["));
            bool first = true;
            for (const auto& item : collector.items) {
                if (!first) out += TJS_W(",");
                first = false;
                out += quoteStructString(item.first);
                out += TJS_W(",");
                out += structString(item.second);
            }
            out += TJS_W("]");
            return out;
        }
        if (object->IsInstanceOf(0, nullptr, nullptr, TJS_W("Array"), object) == TJS_S_TRUE) {
            tTJSVariant count;
            object->PropGet(0, TJS_W("count"), nullptr, &count, object);
            ttstr out(TJS_W("(const)["));
            for (tjs_int i = 0; i < static_cast<tjs_int>(count.AsInteger()); ++i) {
                tTJSVariant element;
                object->PropGetByNum(TJS_IGNOREPROP, i, &element, object);
                if (i != 0) out += TJS_W(",");
                out += structString(element);
            }
            out += TJS_W("]");
            return out;
        }
        return ttstr();
    }
    if (value.Type() == TJS::tvtString) return quoteStructString(ttstr(value));
    return TJS::TJSVariantToExpressionString(value);
}

} // namespace

CM3D2_KagScript::CM3D2_KagScript(void* tjs_script_pointer) {
    cm3d2::internal::TjsScriptAccess access;
    if (!cm3d2::internal::tjsScriptAccess(tjs_script_pointer, access) || access.engine == nullptr) return;
    tjs_script_ = tjs_script_pointer;
    host_.engine = access.engine;
    host_.script = tjs_script_pointer;
    host_.file_system = access.host_context;  // native: parser +0x138 = TjsScript host context
    unique_id_ = g_next_unique_id++;

    HostScope scope(host_);
    iTJSDispatch2* parser_class = TVPCreateNativeClass_KAGParser();
    if (parser_class == nullptr) return;
    parser_class->CreateNew(0, nullptr, nullptr, &parser_object_, 0, nullptr, parser_class);
    parser_class->Release();
    parser_ = tTJSNI_KAGParser::FromObject(parser_object_);
    if (parser_ == nullptr) {
        if (parser_object_ != nullptr) parser_object_->Release();
        parser_object_ = nullptr;
        return;
    }
    installHooks();
    registerDefaultTags();
}

CM3D2_KagScript::~CM3D2_KagScript() {
    if (parser_object_ != nullptr) {
        HostScope scope(host_);
        parser_->OnLoadHook = nullptr;
        parser_->OnLabelHook = nullptr;
        parser_->OnJumpHook = nullptr;
        parser_->OnCallHook = nullptr;
        parser_->OnReturnHook = nullptr;
        parser_->OnLabelPassHook = nullptr;
        parser_object_->Release();
    }
}

// A [jump]/[call] storage without an extension gets ".ks" before the load callbacks see it (kag_real_probe on a missing
// file: the original logs "nofile.ks" for file="nofile"; whether the replace callback runs before or after is UNVERIFIED).
std::wstring CM3D2_KagScript::normalizeJumpStorage(const std::wstring& storage) const {
    std::wstring result = applyReplace(storage);
    if (!result.empty() && result.find(L'.') == std::wstring::npos) result += L".ks";
    return result;
}

// The five parser callbacks KagScript::Init installs (lambdas FUN_180105170 / 105320 / 1801054d0 / 105780 / 105c80).
// Each ends by clearing the paused byte (+0x2b0). Which parser event fires which hook was read from the DLL parser
// (LoadScenario +0x1b8, GoToLabel +0x1f8, jump +0x238, call +0x278, return +0x2b8).
void CM3D2_KagScript::installHooks() {
    parser_->OnLoadHook = [this](const ttstr& name) {
        if (on_scenario_load_function) on_scenario_load_function(this, toUtf8(name).c_str(), "");
        paused_ = false;
    };
    parser_->OnLabelHook = [this](const ttstr& name) {
        if (on_scenario_load_function) on_scenario_load_function(this, "", toUtf8(name).c_str());
        paused_ = false;
    };
    parser_->OnJumpHook = [this](ttstr& storage, ttstr&) {
        storage = toTtstr(normalizeJumpStorage(storage.c_str()));
        paused_ = false;
    };
    parser_->OnCallHook = [this](ttstr& storage, ttstr& label) {
        storage = toTtstr(normalizeJumpStorage(storage.c_str()));
        if (on_call_function) on_call_function(this, toUtf8(storage).c_str(), toUtf8(label).c_str());
        paused_ = false;
    };
    // FUN_180103150 (parser +0x140): was this (storage, label) visited before? Report it, then record it.
    parser_->OnLabelPassHook = [this](const ttstr& label, bool, const ttstr&) {
        const std::wstring storage = parser_->GetStorageName().c_str();
        const bool seen = records_.hasLabel(storage, label.c_str());
        if (label_function) label_function(this, seen ? 1 : 0);
        if (!seen) records_.addLabel(storage, label.c_str());
    };
    parser_->OnReturnHook = [this](const ttstr& storage, tjs_int depth) {
        if (on_return_function) on_return_function(this, toUtf8(storage).c_str(), depth);
        paused_ = false;
    };
}

void CM3D2_KagScript::registerDefaultTags() {
    // KagScript::Init: the three base tags, then the 19 of table 0x180215620 (names read from the binary).
    addTag(L"ch", [this](CM3D2_KagTagSupport* s) { return tagCh(s); });
    addTag(L"eval", [this](CM3D2_KagTagSupport* s) { return tagEval(s); });
    addTag(L"r", [](CM3D2_KagTagSupport*) { return false; });  // FUN_18006a5b0: no-op (confirmed: [r] adds no text)
    addTag(L"r_call", [this](CM3D2_KagTagSupport* s) { return tagRCall(s); });
    addTag(L"r_return", [this](CM3D2_KagTagSupport* s) { return tagRReturn(s); });
    static const wchar_t* const kReEmit[] = {L"hf", L"hl", L"hlf", L"hf1", L"hl1", L"hlf1", L"hf2", L"hl2", L"hlf2", L"hf3", L"hl3",
        L"hlf3", L"hf4", L"hl4", L"hlf4", L"sf"};
    for (const wchar_t* name : kReEmit) addTag(name, [this](CM3D2_KagTagSupport* s) { return tagReEmit(s); });
    addTag(L"s", [this](CM3D2_KagTagSupport* s) { return tagStop(s); });
}

std::wstring CM3D2_KagScript::applyReplace(const std::wstring& text) const {
    if (replace_function == nullptr) return text;
    char buffer[0x200] = {};
    replace_function(this, toUtf8(text).c_str(), buffer, 0x200);
    return fromUtf8(buffer);
}

void CM3D2_KagScript::report(const std::wstring& message) const {
    if (tjs_script_ != nullptr) cm3d2::internal::tjsReportException(tjs_script_, message);
}

void CM3D2_KagScript::loadScenario(const std::wstring& storage) {
    if (!parser_) return;
    HostScope scope(host_);
    const std::wstring previous_storage = parser_->GetStorageName().c_str();
    try {
        parser_->LoadScenario(toTtstr(storage));
    } catch (TJS::eTJS& error) {
        // Same report as a failed load inside [call]/[jump] (kag_console_probe: LoadScenario, r_call, r_return): tag
        // "eTJSError", the message, then the storage that was current before the load and the parser's line.
        std::wstring message = L"[eTJSError]";
        message += error.GetMessage().c_str();
        message += L"\nファイル : ";
        message += previous_storage;
        message += L"   行 : ";
        message += std::to_wstring(parser_->GetCurLine());
        report(message);
        return;
    } catch (...) {
        report(L"KAG: load error");
        return;
    }
    running_ = true;
}

void CM3D2_KagScript::loadScenarioString(const std::wstring& text) {
    if (!parser_) return;
    HostScope scope(host_);
    try {
        parser_->LoadScenarioString(toTtstr(text));
    } catch (TJS::eTJS& error) {
        report(error.GetMessage().c_str());
        return;
    } catch (...) {
        report(L"KAG: load error");
        return;
    }
    running_ = true;
}

void CM3D2_KagScript::goToLabel(const std::wstring& label) {
    if (!parser_) return;
    HostScope scope(host_);
    try {
        parser_->GoToLabel(toTtstr(label));
    } catch (TJS::eTJS& error) {
        report(error.GetMessage().c_str());
        return;
    } catch (...) {
        report(L"KAG: label error");
        return;
    }
    running_ = true;
}

// FUN_180101ff0.
int CM3D2_KagScript::exec() {
    if (!parser_ || !running_) return 0;
    HostScope scope(host_);
    if (paused_) return 1;  // BasicKagScript slot 9: byte +0x2b0
    ttstr last_tag_name;  // the tag Exec handled last in this call; the error text names it (checked on real scenarios)
    for (;;) {
        iTJSDispatch2* tag = nullptr;
        try {
            tag = parser_->GetNextTag();
        } catch (TJS::eTJS& error) {
            // The original catches parser errors (for example a [return] without a [call]) inside Exec: the message goes to
            // the console, Exec returns 0 and the script keeps running (confirmed with kag_flow_probe). The text is the
            // message table entry at 0x1803c9ed0; %3 is the last tag returned in this Exec call (unreplaced before the first
            // one), confirmed with kag_console_probe and the TJS-mode real scenario sweep.
            std::wstring message = L"エラーが発生しました\nファイル : ";
            message += parser_->GetStorageName().c_str();
            message += L"   行 : ";
            message += std::to_wstring(parser_->GetCurLine() + 1);
            message += L"\nタグ : ";
            message += last_tag_name.IsEmpty() ? L"%3" : last_tag_name.c_str();
            message += L"\n";
            message += error.GetMessage().c_str();
            message += L"\n";
            report(message);
            return 0;
        } catch (...) {
            report(L"KAG: parser error");
            return 0;
        }
        if (tag == nullptr) {
            running_ = false;
            return 0;
        }
        tTJSVariant name_value;
        tag->PropGet(0, TJS_W("tagname"), nullptr, &name_value, tag);
        const ttstr tag_name(name_value);
        last_tag_name = tag_name;
        const std::uint64_t key = hashUtf16(tag_name.c_str());
        bool stop = false;
        const auto found = tags_.find(key);
        if (found == tags_.end()) {
            // "unknown tag" text read at 0x1801e9f30; %1 tag, %2 storage, %3 line (no +1 here).
            std::wstring message = L"未定義タグエラーが発生しました : ";
            message += tag_name.c_str();
            message += L"\nファイル : ";
            message += parser_->GetStorageName().c_str();
            message += L"   行 : ";
            message += std::to_wstring(parser_->GetCurLine());
            report(message);
        } else {
            CM3D2_KagTagSupport support;
            support.dict = tag;
            support.key = key;
            support.owner = this;
            try {
                stop = found->second.function(&support);
            } catch (TJS::eTJS& error) {
                report(error.GetMessage().c_str());
                tag->Release();
                return 0;  // same treatment as parser errors (UNVERIFIED for handlers)
            } catch (...) {
                report(L"KAG: tag handler error");
                tag->Release();
                return 0;
            }
        }
        tag->Release();
        if (stop) return 1;
    }
}

void CM3D2_KagScript::clearCallStack() {
    if (parser_) {
        HostScope scope(host_);
        parser_->ClearCallStackExternal();
    }
    calls_.clear();
}

int CM3D2_KagScript::callStackDepth() const { return parser_ ? parser_->GetCallStackDepthExternal() : 0; }
std::wstring CM3D2_KagScript::storageName() const { return parser_ ? std::wstring(parser_->GetStorageName().c_str()) : std::wstring(); }
std::wstring CM3D2_KagScript::currentLabel() const { return parser_ ? std::wstring(parser_->GetCurLabel().c_str()) : std::wstring(); }
int CM3D2_KagScript::currentLine() const { return parser_ ? parser_->GetCurLine() : 0; }
void CM3D2_KagScript::setTagCallEnabled(bool enabled) { if (parser_) parser_->TagCallEnabled = enabled; }
bool CM3D2_KagScript::tagCallEnabled() const { return parser_ ? parser_->TagCallEnabled : false; }
void CM3D2_KagScript::setTagReturnEnabled(bool enabled) { if (parser_) parser_->TagReturnEnabled = enabled; }
bool CM3D2_KagScript::tagReturnEnabled() const { return parser_ ? parser_->TagReturnEnabled : false; }

namespace {
// The parser lowercases tag names, so registered names are lowercased too (confirmed with real scenario files:
// a handler added as "MotionScript" receives [MotionScript]).
std::wstring lowered(const std::wstring& name) {
    ttstr text(name.c_str());
    text.ToLowerCase();
    return text.c_str();
}
} // namespace

void CM3D2_KagScript::addTag(const std::wstring& raw_name, std::function<bool(CM3D2_KagTagSupport*)> function) {
    // FUN_180101380: an existing key keeps its handler (confirmed: a second AddTag with the same name is ignored).
    const std::wstring name = lowered(raw_name);
    const std::uint64_t key = hashUtf16(name);
    if (tags_.find(key) != tags_.end()) return;
    tags_[key] = TagEntry{name, std::move(function)};
    tag_names_[key] = name;
}

std::uint64_t CM3D2_KagScript::tagKey(const std::wstring& raw_name) const { return hashUtf16(lowered(raw_name)); }

bool CM3D2_KagScript::removeTag(const std::wstring& raw_name) {
    const std::uint64_t key = hashUtf16(lowered(raw_name));
    tag_names_.erase(key);
    return tags_.erase(key) != 0;
}

// DLL_KAG_TAG_CreateTagList (FUN_18006fbf0): key array of the tag dictionary, then each value as text.
void CM3D2_KagScript::createTagList(CM3D2_KagTagSupport& support) {
    support.list.clear();
    if (support.dict == nullptr) return;
    HostScope scope(host_);
    MemberCollector collector;
    tTJSVariantClosure closure(&collector, nullptr);
    support.dict->EnumMembers(TJS_IGNOREPROP | TJS_ENUM_NO_VALUE, &closure, support.dict);
    for (const auto& item : collector.items) {
        tTJSVariant value;
        support.dict->PropGet(0, item.first.c_str(), nullptr, &value, support.dict);
        support.list.emplace_back(toUtf8(item.first), toUtf8(ttstr(value)));
    }
}

// FUN_180103240: appends the "text" attribute (a string) to the script text.
bool CM3D2_KagScript::tagCh(CM3D2_KagTagSupport* support) {
    tTJSVariant value;
    support->dict->PropGet(0, TJS_W("text"), nullptr, &value, support->dict);
    if (value.Type() == TJS::tvtString) text_ += ttstr(value).c_str();  // other types: the original throws (UNVERIFIED hardening)
    return false;
}

// FUN_1801032d0: evaluates the "exp" attribute; on failure prints the @eval message and stops.
bool CM3D2_KagScript::tagEval(CM3D2_KagTagSupport* support) {
    if (support->dict->IsValid(0, TJS_W("exp"), nullptr, support->dict) != TJS_S_TRUE) return false;
    tTJSVariant value;
    support->dict->PropGet(0, TJS_W("exp"), nullptr, &value, support->dict);
    const ttstr expression(value);
    if (cm3d2::internal::tjsRunExpression(tjs_script_, expression.c_str(), nullptr)) return false;
    std::wstring message = L"@eval エラー\nファイル : ";  // text at 0x1801e9f80; %1 storage, %2 line + 1
    message += parser_->GetStorageName().c_str();
    message += L"   行 : ";
    message += std::to_wstring(parser_->GetCurLine() + 1);
    report(message);
    return true;
}

// FUN_180106170: hf/hl/hlf... and sf re-emit themselves as "[name]" text (the host handles them later).
bool CM3D2_KagScript::tagReEmit(CM3D2_KagTagSupport* support) {
    const auto found = tag_names_.find(support->key);
    if (found != tag_names_.end()) text_ += L"[" + found->second + L"]";
    return false;
}

// 0x180104360: sets the paused byte (+0x2b0 = 1) and returns true, which stops Exec.
bool CM3D2_KagScript::tagStop(CM3D2_KagTagSupport*) {
    paused_ = true;
    return true;
}

// FUN_180106490: recorded call. The file name rule was found by probing the original (UNVERIFIED against the
// pseudocode, which inlines it): a name with a dot becomes <text after the last dot> twice ("b.ks" -> "ksks"), a name
// without one gets ".ks" appended ("b" -> "b.ks"). The replace callback sees the part before the extension is added.
// The pushed record is (current storage, "", id); the new storage is loaded and execution continues (the tag returns false).
bool CM3D2_KagScript::tagRCall(CM3D2_KagTagSupport* support) {
    tTJSVariant value;
    support->dict->PropGet(0, TJS_W("file"), nullptr, &value, support->dict);
    const std::wstring file = ttstr(value).c_str();
    std::int32_t id = -1;
    if (support->dict->IsValid(0, TJS_W("id"), nullptr, support->dict) == TJS_S_TRUE) {
        tTJSVariant id_value;
        support->dict->PropGet(0, TJS_W("id"), nullptr, &id_value, support->dict);
        id = static_cast<std::int32_t>(id_value.AsInteger());
    }
    std::wstring base = file;
    std::wstring extension = L".ks";
    const std::size_t dot = file.rfind(L'.');
    if (dot != std::wstring::npos) {
        base = file.substr(dot + 1);
        extension = base;
    }
    const std::wstring replaced_base = applyReplace(base);
    const std::wstring storage = replaced_base + extension;
    if (on_rcall_function) on_rcall_function(this, toUtf8(storage).c_str(), "");
    // The record keeps the (replaced) base name as its label; r_return later jumps to "*RET_" + label (+ "_" + id), the
    // convention of the scenarios (@R_call file="X" ... *RET_X). Found with kag_save_probe/real scenarios: the record label in the
    // original save data is the base name, and a script with *RET_ labels continues after the call.
    calls_.push(parser_->GetStorageName().c_str(), replaced_base, id);
    loadScenario(storage);
    paused_ = false;
    return false;
}

bool CM3D2_KagScript::tagRReturn(CM3D2_KagTagSupport*) {
    std::wstring storage;
    std::wstring label;
    if (!calls_.pop(storage, label)) {
        // Empty record list: the original still loads the (empty) storage, which fires the load callback with ("", "") and
        // leaves the parser without a storage name (c1_vip_04400.ks ends with an unmatched [R_return]); no return callback.
        loadScenario(std::wstring());
        return false;
    }
    if (on_rreturn_function) on_rreturn_function(this, toUtf8(storage).c_str(), toUtf8(label).c_str());
    loadScenario(storage);
    goToLabel(label);
    paused_ = false;
    return false;
}

CM3D2_KagSerializeData* CM3D2_KagScript::createSerializeData() {
    HostScope scope(host_);
    std::optional<std::wstring> state_text;
    if (parser_) {
        iTJSDispatch2* dictionary = parser_->Store();
        if (dictionary != nullptr) {
            const tTJSVariant variant(dictionary, dictionary);
            state_text = std::wstring(structString(variant).c_str());
            dictionary->Release();
        }
    }
    const std::vector<std::uint8_t> bytes = cm3d2::kag::serializeSaveData(records_, state_text, paused_, calls_);
    auto* data = new CM3D2_KagSerializeData;
    data->data = static_cast<std::uint8_t*>(std::malloc(bytes.size() == 0 ? 1 : bytes.size()));
    std::memcpy(data->data, bytes.data(), bytes.size());
    data->size = static_cast<std::int64_t>(bytes.size());
    return data;
}

std::int64_t CM3D2_KagScript::deserialize(const void* data) {
    HostScope scope(host_);
    const auto parsed = cm3d2::kag::parseSaveData(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(1) << 30, records_, calls_);
    if (parser_) {
        try {
            parser_->Clear();
            if (parsed.state_text.has_value()) {
                tTJSVariant result;
                if (cm3d2::internal::tjsRunExpression(tjs_script_, *parsed.state_text, &result) && result.Type() == TJS::tvtObject) {
                    parser_->Restore(result.AsObjectNoAddRef());
                }
            }
        } catch (TJS::eTJS& error) {
            report(error.GetMessage().c_str());
        } catch (...) {
            report(L"KAG: restore error");
        }
    }
    paused_ = parsed.flag;
    return static_cast<std::int64_t>(parsed.consumed);
}
