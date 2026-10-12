#include "cm3d2/tjs_api.h"

#include "file_interface.h"
#include "file_system_facade.h"
#include "tjs_script_internal.h"
#include "tjs_variant_internal.h"
#include "tjs_messages.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsError.h"
#include "tjsObject.h"
#include "tjsNative.h"
#include "tjsVariant.h"

// Reconstruction of the TjsScriptCustom wrapper (vtable 0x1801dcdc8) around the TJS2 engine.
// The engine is a substituted vendored Kirikiri Z TJS2 core (third_party/tjs2), not a recovered native VM;
// the wrapper follows
// FUN_1801086d0 (init), FUN_180109c70 (exec/eval), FUN_180108960 / FUN_180108c50 (file variants),
// FUN_180100500 (file text loader), FUN_180109210 (AddMemberFunction), FUN_180108f80 (OverseeDispatch::FuncCall)
// and FUN_1800737f0 / FUN_1800739e0 (console delegate).
// UNVERIFIED: the engine's own behaviour (embedded upstream, not rebuilt from pseudocode); the built-in
// RegExp is not provided. Script exceptions are caught at the wrapper boundary;
// Engine version and unsupported built-ins remain compatibility limits; see the project notes (not part of this repository).

struct CM3D2_TjsParamAccess {  // ParamAccess: +0 args, +8 count, +0x10 name key, +0x18 class unique id
    cm3d2::internal::TjsVariant** arguments;
    std::int32_t count;
    std::uint64_t name_key;
    std::uint64_t class_unique_id;
};

namespace {

using cm3d2::internal::IFile;
using cm3d2::internal::TjsVariant;

constexpr std::uint32_t kMemberEnsure = 0x00000200;  // TJS_MEMBERENSURE

std::atomic<std::uint64_t> g_next_unique_id{0};  // DAT_18021dc68
std::atomic<int> g_instance_count{0};            // _DAT_18021dc70

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    }
    return out;
}

std::wstring fromCodePage(UINT code_page, const char* bytes, std::size_t length) {
    if (length == 0) return {};
    const int size = MultiByteToWideChar(code_page, 0, bytes, static_cast<int>(length), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(code_page, 0, bytes, static_cast<int>(length), out.data(), size);
    }
    return out;
}

// The hash of FUN_180109210: FNV-1a 64 over the UTF-16 bytes of the name.
std::uint64_t nameKey(const std::wstring& name) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(name.data());
    for (std::size_t i = 0; i < name.size() * 2; ++i) {
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    }
    return hash;
}

class Script;

// Semantic lifetime guard, not a recovered native field. Native destroys its
// callback map before VM shutdown (FUN_180073260), leaving retained dispatches
// with an unsafe owner. Keep the map alive during shutdown and reject dispatch
// after the wrapper has gone away instead of reproducing that use-after-free.
struct ScriptLifetime { Script* owner; };

// OverseeDispatch (FUN_180108f80): a global function object that forwards calls to a registered host callback.
class OverseeDispatch : public TJS::tTJSDispatch {
public:
    OverseeDispatch(std::shared_ptr<ScriptLifetime> lifetime, std::uint64_t key)
        : lifetime_(std::move(lifetime)), key_(key) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, TJS::tTJSVariant* result,
        tjs_int numparams, TJS::tTJSVariant** param, TJS::iTJSDispatch2*) override;
private:
    std::shared_ptr<ScriptLifetime> lifetime_;
    std::uint64_t key_;
};

class Console : public TJS::iTJSConsoleOutput {
public:
    explicit Console(CM3D2_TjsConsoleCallback callback) : callback_(callback) {}
    void ExceptionPrint(const tjs_char* message) override { callback_(toUtf8(message).c_str(), 1); }
    void Print(const tjs_char* message) override { callback_(toUtf8(message).c_str(), 0); }
private:
    CM3D2_TjsConsoleCallback callback_;
};

class Script {
public:
    explicit Script(void* host_context) : host_context_(host_context), unique_id_(g_next_unique_id++),
        lifetime_(std::make_shared<ScriptLifetime>(ScriptLifetime{this})) {
        cm3d2::internal::initializeTjsMessages();
        engine_ = new TJS::tTJS();
        try {
            installScripts();
        } catch (...) {
            lifetime_->owner = nullptr;
            engine_->Shutdown();
            engine_->Release();
            throw;
        }
        ++g_instance_count;
    }
    ~Script() {
        if (engine_ != nullptr) {
            engine_->SetConsoleOutput(nullptr);
            engine_->Shutdown();
            engine_->Release();
        }
        lifetime_->owner = nullptr;
        --g_instance_count;
    }

    void setConsole(CM3D2_TjsConsoleCallback callback) {
        console_.reset(callback == nullptr ? nullptr : new Console(callback));
        engine_->SetConsoleOutput(console_.get());
    }
    void clearConsole() {
        engine_->SetConsoleOutput(nullptr);
        console_.reset();
    }

    void report(const std::wstring& message) {
        if (console_) console_->ExceptionPrint(message.c_str());
    }
    void log(const std::wstring& message) {
        if (console_) console_->Print(message.c_str());
    }

    // FUN_180109c70: 0 = ExecScript, 1 = EvalExpression. A null/empty script does nothing.
    void run(const std::wstring& text, bool evaluate, TjsVariant* result) {
        runNative(text, evaluate, result != nullptr ? &result->native() : nullptr);
    }

    // Scripts callbacks keep VM values directly inside the VM.
    bool runNative(const std::wstring& text, bool evaluate, TJS::tTJSVariant* result) {
        if (text.empty()) return false;
        try {
            if (evaluate) {
                engine_->EvalExpression(text.c_str(), result);
            } else {
                engine_->ExecScript(text.c_str(), result);
            }
        } catch (TJS::eTJSScriptError& error) {
            // Native catch funclets FUN_1801b11e0/1801b12d0 format the error
            // message (despite the literal "block name" label), not GetBlockName().
            const std::wstring prefix = evaluate ? L"TjsScript::EvalExpression error." : L"TjsScript::ExecScript error.";
            report(prefix + L"\n\u884c : " + std::to_wstring(error.GetSourceLine()) +
                L" \u30d6\u30ed\u30c3\u30af\u540d : " + error.GetMessage().c_str());
            return false;
        } catch (...) {
            report(evaluate ? L"TjsScript::EvalExpression error." : L"TjsScript::ExecScript erro.r");
            return false;
        }
        return true;
    }

    // FUN_180108960 / FUN_180108c50.
    void runFile(IFile* file, bool evaluate, TjsVariant* result) {
        runNativeFile(file, evaluate, result != nullptr ? &result->native() : nullptr);
    }

    bool runNativeFile(IFile* file, bool evaluate, TJS::tTJSVariant* result) {
        const wchar_t* label = evaluate ? L"TjsScript::EvalScript error. [" : L"TjsScript::ExecScript error. [";
        if (file == nullptr || file->size() < 2) {
            report(std::wstring(label) + L"file not found.]");
            return false;
        }
        std::wstring text;
        std::wstring failure;
        if (!loadText(file, text, failure)) {
            report(std::wstring(label) + failure + L"]");
            return false;
        }
        return runNative(text, evaluate, result);
    }

    void runStorage(const std::wstring& path, bool evaluate, TJS::tTJSVariant* result) {
        // FUN_1800fff60/1801000c0: wide filesystem slot 0 transfers ownership;
        // its returned file is deleted after the nested wrapper call.
        std::unique_ptr<IFile> file(cm3d2::internal::fileSystemOpenWide(host_context_, path));
        runNativeFile(file.get(), evaluate, result);
    }

    // Returns the name key (FNV-1a over the UTF-16 bytes of the name), exactly what FUN_180109210 returns and what
    // ParamAccess_GetNameKey reports inside the callback; the game dispatches on it.
    std::uint64_t addMember(const std::wstring& name, CM3D2_TjsMemberFunction function) {
        const std::uint64_t key = nameKey(name);
        callbacks_.erase(key);
        TJS::iTJSDispatch2* global = engine_->GetGlobalNoAddRef();
        auto* dispatch = new OverseeDispatch(lifetime_, key);
        TJS::tTJSVariant value(dispatch, nullptr);
        dispatch->Release();
        global->PropSet(kMemberEnsure, name.c_str(), nullptr, &value, global);
        if (function != nullptr) callbacks_[key] = function;
        return key;
    }

    CM3D2_TjsMemberFunction callback(std::uint64_t key) const {
        const auto found = callbacks_.find(key);
        return found == callbacks_.end() ? nullptr : found->second;
    }
    std::uint64_t uniqueId() const { return unique_id_; }

private:
    void installScripts();
    // FUN_180100500: BOM detection, then UTF-16LE, UTF-8 or code page 932.
    static bool loadText(IFile* file, std::wstring& text, std::wstring& failure) {
        const std::uint64_t size = file->size();
        if (size == 0 || size > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
            failure = L"file not found.";  // Oversized input rejection is deliberate hardening.
            return false;
        }
        // Native BOM probing starts at the current file position. It rewinds
        // only when selecting a decoding branch (size <= 3 skips the probe).
        std::vector<char> bytes(static_cast<std::size_t>(size));
        if (size > 3) {
            unsigned char bom[4] = {};
            file->read(bom, 4);
            if (bom[0] == 0xFF && bom[1] == 0xFE) {
                if (bom[2] == 0 && bom[3] == 0) {
                    failure = L"not support UTF-32";
                    return false;
                }
                file->seek(2, true);
                const std::uint64_t units = (size - 2) >> 1;
                text.assign(units, L'\0');
                file->read(text.data(), units * 2);
                return true;
            }
            if (bom[0] == 0xFE && bom[1] == 0xFE) {  // as printed by the decompiler (a big endian BOM is FE FF)
                failure = L"not support Big Endians UTF-16 or UTF-32";
                return false;
            }
            if (bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
                file->seek(3, true);
                const std::uint64_t length = size - 3;
                bytes.assign(static_cast<std::size_t>(length), '\0');
                file->read(bytes.data(), length);
                text = fromCodePage(CP_UTF8, bytes.data(), bytes.size());
                return true;
            }
            file->seek(0, true);
        }
        file->read(bytes.data(), size);
        text = fromCodePage(932, bytes.data(), bytes.size());
        return true;
    }

public:
    // Seam for the KAG group (src/kag_host.cpp): read-only access to engine, host context and the file text decoder.
    TJS::tTJS* engine() const { return engine_; }
    void* hostContext() const { return host_context_; }
    static bool decodeFileText(IFile* file, std::wstring& text, std::wstring& failure) { return loadText(file, text, failure); }

private:
    void* host_context_;  // borrowed wide filesystem interface, native +0x10
    std::uint64_t unique_id_;
    std::shared_ptr<ScriptLifetime> lifetime_;
    TJS::tTJS* engine_ = nullptr;
    std::unique_ptr<Console> console_;
    std::unordered_map<std::uint64_t, CM3D2_TjsMemberFunction> callbacks_;
};

// Native init FUN_1801086d0 attaches the owner to the registered Scripts class.
// Use a semantic native binding, not the original class's raw +0xa0 layout.
struct ScriptsBinding final : TJS::tTJSNativeInstance {
    explicit ScriptsBinding(std::shared_ptr<ScriptLifetime> script_lifetime)
        : lifetime(std::move(script_lifetime)) {}
    std::shared_ptr<ScriptLifetime> lifetime;
};

class ScriptsClass final : public TJS::tTJSNativeClass {
public:
    explicit ScriptsClass(std::shared_ptr<ScriptLifetime> lifetime) : tTJSNativeClass(TJS::ttstr(L"Scripts")) {
        _ClassID = TJS::TJSRegisterNativeClass(L"Scripts");
        TJS::iTJSNativeInstance* binding = new ScriptsBinding(std::move(lifetime));
        NativeInstanceSupport(TJS_NIS_REGISTER, _ClassID, &binding);
        // FUN_1800ff8c0 registers these exact six members and flags.
        RegisterNCM(L"finalize", TJS::TJSCreateNativeClassMethod(noop), L"Scripts", TJS::nitMethod);
        RegisterNCM(L"Scripts", TJS::TJSCreateNativeClassConstructor(noop), L"Scripts", TJS::nitMethod);
        RegisterNCM(L"execStorage", TJS::TJSCreateNativeClassMethod(execStorage), L"Scripts", TJS::nitMethod, TJS_STATICMEMBER);
        RegisterNCM(L"evalStorage", TJS::TJSCreateNativeClassMethod(evalStorage), L"Scripts", TJS::nitMethod, TJS_STATICMEMBER);
        RegisterNCM(L"exec", TJS::TJSCreateNativeClassMethod(exec), L"Scripts", TJS::nitMethod, TJS_STATICMEMBER);
        RegisterNCM(L"eval", TJS::TJSCreateNativeClassMethod(eval), L"Scripts", TJS::nitMethod, TJS_STATICMEMBER);
    }

private:
    static Script* ownerOf(TJS::iTJSDispatch2* receiver) {
        TJS::iTJSNativeInstance* binding = nullptr;
        if (receiver == nullptr || TJS_FAILED(receiver->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            TJS::TJSFindNativeClassID(L"Scripts"), &binding)) || binding == nullptr) return nullptr;
        return static_cast<ScriptsBinding*>(binding)->lifetime->owner;
    }

    static tjs_error TJS_INTF_METHOD noop(TJS::tTJSVariant*, tjs_int, TJS::tTJSVariant**, TJS::iTJSDispatch2*) {
        return TJS_S_OK; // native constructor/finalize FUN_180069ef0
    }

    static tjs_error invoke(TJS::tTJSVariant* result, tjs_int count, TJS::tTJSVariant** args,
        TJS::iTJSDispatch2* receiver, bool evaluate, bool storage) {
        Script* owner = ownerOf(receiver);
        if (owner == nullptr) return TJS_E_NATIVECLASSCRASH;
        if (count < 1) return TJS_E_BADPARAMCOUNT;
        const TJS::ttstr source(*args[0]);
        TJS::ttstr optional;
        if (count > 1 && (!storage || args[1]->Type() != TJS::tvtVoid)) optional = TJS::ttstr(*args[1]);
        if (storage) {
            // Native evalStorage really opens argument 1, unlike execStorage.
            // Confirmed in assembly at 0x180100181..18010019f; do not "correct" it.
            owner->runStorage(evaluate ? optional.c_str() : source.c_str(), evaluate, result);
        } else {
            // Name and line offset are converted, but are not forwarded by the
            // native wrapper. Conversion errors must still be observable.
            if (count > 2) (void)args[2]->AsInteger();
            owner->runNative(source.c_str(), evaluate, result);
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD exec(TJS::tTJSVariant* r, tjs_int n, TJS::tTJSVariant** p, TJS::iTJSDispatch2* o) {
        return invoke(r, n, p, o, false, false);
    }
    static tjs_error TJS_INTF_METHOD eval(TJS::tTJSVariant* r, tjs_int n, TJS::tTJSVariant** p, TJS::iTJSDispatch2* o) {
        return invoke(r, n, p, o, true, false);
    }
    static tjs_error TJS_INTF_METHOD execStorage(TJS::tTJSVariant* r, tjs_int n, TJS::tTJSVariant** p, TJS::iTJSDispatch2* o) {
        return invoke(r, n, p, o, false, true);
    }
    static tjs_error TJS_INTF_METHOD evalStorage(TJS::tTJSVariant* r, tjs_int n, TJS::tTJSVariant** p, TJS::iTJSDispatch2* o) {
        return invoke(r, n, p, o, true, true);
    }
};

void Script::installScripts() {
    auto* native_class = new ScriptsClass(lifetime_);
    // Native local_30/local_28 both hold the class: bind its receiver too.
    TJS::tTJSVariant value(native_class, native_class);
    native_class->Release();
    auto* global = engine_->GetGlobalNoAddRef();
    global->PropSet(TJS_MEMBERENSURE, L"Scripts", nullptr, &value, global);
}

tjs_error TJS_INTF_METHOD OverseeDispatch::FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*,
    TJS::tTJSVariant* result, tjs_int numparams, TJS::tTJSVariant** param, TJS::iTJSDispatch2*) {
    Script* owner = lifetime_->owner;
    if (owner == nullptr) return TJS_E_INVALIDOBJECT;
    const CM3D2_TjsMemberFunction function = owner->callback(key_);
    if (function != nullptr) {
        std::vector<TjsVariant> storage(static_cast<std::size_t>(numparams > 0 ? numparams : 0));
        std::vector<TjsVariant*> arguments(storage.size());
        std::unordered_map<TJS::tTJSVariant*, TjsVariant*> views;
        views.reserve(storage.size());
        for (std::size_t i = 0; i < storage.size(); ++i) {
            storage[i].borrowed = param[i];
            // Preserve aliasing if the VM supplied the same slot more than once.
            arguments[i] = views.emplace(param[i], &storage[i]).first->second;
        }
        CM3D2_TjsParamAccess access{arguments.data(), numparams, key_, owner->uniqueId()};
        TjsVariant host_result(result);
        TjsVariant* result_view = result != nullptr ? &host_result : nullptr;
        const auto found_result = views.find(result);
        if (found_result != views.end()) result_view = found_result->second;
        function(&access, result_view);
    }
    return 0;
}

Script* script(CM3D2_TjsScriptHandle* handle) { return static_cast<Script*>(*handle); }

std::wstring wide(const char* utf8) { return utf8 == nullptr ? std::wstring() : fromCodePage(CP_UTF8, utf8, std::strlen(utf8)); }

} // namespace

namespace cm3d2::internal {

bool tjsScriptAccess(void* script_pointer, TjsScriptAccess& out) {
    if (script_pointer == nullptr) return false;
    const auto* object = static_cast<const Script*>(script_pointer);
    out.engine = object->engine();
    out.host_context = object->hostContext();
    return true;
}

bool tjsDecodeFileText(IFile* file, std::wstring& text, std::wstring& failure) {
    return Script::decodeFileText(file, text, failure);
}

// KAG seam: evaluate an expression through the TjsScript wrapper (errors are printed to its console, as the DLL does).
bool tjsRunExpression(void* script_pointer, const std::wstring& text, TJS::tTJSVariant* result) {
    return static_cast<Script*>(script_pointer)->runNative(text, true, result);
}

void tjsReportException(void* script_pointer, const std::wstring& message) {
    static_cast<Script*>(script_pointer)->report(message);
}

void tjsLogMessage(void* script_pointer, const std::wstring& message) {
    static_cast<Script*>(script_pointer)->log(message);
}

} // namespace cm3d2::internal

extern "C" {

void CM3D2_CALL DLL_TJS_CreateTjsScriptClass(void* host_context, CM3D2_TjsScriptHandle* out_script) {
    *out_script = new Script(host_context);
}

void CM3D2_CALL DLL_TJS_CreateTjsConsoleClass(CM3D2_TjsScriptHandle* handle, CM3D2_TjsConsoleCallback callback) {
    script(handle)->setConsole(callback);
}

void CM3D2_CALL DLL_TJS_DeleteTjsScriptClass(CM3D2_TjsScriptHandle* handle) {
    if (*handle != nullptr) {
        script(handle)->clearConsole();
    }
    delete script(handle);
    *handle = nullptr;
}

void CM3D2_CALL DLL_TJS_ExecScript(CM3D2_TjsScriptHandle* handle, const char* utf8, void* result) {
    if (utf8 != nullptr) script(handle)->run(wide(utf8), false, static_cast<TjsVariant*>(result));
}

void CM3D2_CALL DLL_TJS_EvalScript(CM3D2_TjsScriptHandle* handle, const char* utf8, void* result) {
    if (utf8 != nullptr) script(handle)->run(wide(utf8), true, static_cast<TjsVariant*>(result));
}

void CM3D2_CALL DLL_TJS_ExecScriptFile(CM3D2_TjsScriptHandle* handle, void* file, void* result) {
    auto* interface_file = static_cast<IFile*>(file);
    if (interface_file != nullptr && interface_file->valid()) {
        script(handle)->runFile(interface_file, false, static_cast<TjsVariant*>(result));
    }
}

void CM3D2_CALL DLL_TJS_EvalScriptFile(CM3D2_TjsScriptHandle* handle, void* file, void* result) {
    auto* interface_file = static_cast<IFile*>(file);
    if (interface_file != nullptr && interface_file->valid()) {
        script(handle)->runFile(interface_file, true, static_cast<TjsVariant*>(result));
    }
}

uint64_t CM3D2_CALL DLL_TJS_AddMemberFunction(CM3D2_TjsScriptHandle* handle, const char* utf8_name, CM3D2_TjsMemberFunction function) {
    return script(handle)->addMember(wide(utf8_name), function);
}

uint64_t CM3D2_CALL DLL_TJS_GetUniqueID(CM3D2_TjsScriptHandle* handle) { return script(handle)->uniqueId(); }

int32_t CM3D2_CALL DLL_TJS_ParamAccess_Size(const CM3D2_TjsParamAccess* p) { return p->count; }
uint64_t CM3D2_CALL DLL_TJS_ParamAccess_GetClassUniqueId(const CM3D2_TjsParamAccess* p) { return p->class_unique_id; }
uint64_t CM3D2_CALL DLL_TJS_ParamAccess_GetNameKey(const CM3D2_TjsParamAccess* p) { return p->name_key; }
void* CM3D2_CALL DLL_TJS_ParamAccess_At(const CM3D2_TjsParamAccess* p, int32_t index) { return p->arguments[index]; }

} // extern "C"
