#include "kag_host.h"

#include "kag_host_context.h"
#include "file_interface.h"
#include "file_system_facade.h"
#include "tjs_script_internal.h"

#include <memory>

// Host services for the embedded Kirikiri KAGParser (third_party/kagparser). The parser source is the open-source
// Kirikiri component, not recovered cm3d2 code; this file replaces the Kirikiri host layer it normally links against.
// Message texts are the defaults read from the DLL (the project notes (not part of this repository)). UNVERIFIED: TVPAddLog destination,
// TVPExtractStorageName details, the compact-event hook (never fired), and how the original reports a missing file.

// Message defaults recovered from cm3d2.dll (holder names at 0x1801e9638..0x1801e9c68; the text follows each name).
const tjs_char* TVPKAGNoLine = L"読み込もうとしたシナリオファイル %1 は空です";
const tjs_char* TVPKAGCannotOmmitFirstLabelName = L"シナリオファイルの最初のラベル名は省略できません";
const tjs_char* TVPKAGFileNotFound = L"シナリオファイル %1 が見つかりません";
const tjs_char* TVPKAGFileNotTextRead = L"シナリオファイルのテキストを読み込めませんでした %1";
const tjs_char* TVPKAGLabelNotFound = L"シナリオファイル %1 内にラベル %2 が見つかりまん";  // sic, as in the binary
const tjs_char* TVPKAGInlineScriptNotEnd = L"[endscript] または @endscript が見つかりません";
const tjs_char* TVPKAGSyntaxError =
    L"タグの文法エラーです。'[' や ']' の対応、\" と \" の対応、スペースの入れ忘れ、余分な改行、macro ～ endmacro の対応、"
    L"必要な属性の不足などを確認してください";
const tjs_char* TVPKAGMacroEntityNotAvailable = L"マクロエンティティはマクロ外では使用できません";
const tjs_char* TVPKAGCallStackUnderflow = L"return タグが call タグと対応していません ( return タグが多い )";
const tjs_char* TVPKAGReturnLostSync = L"シナリオファイルに変更があったため return の戻り先位置を特定できません";
const tjs_char* TVPKAGSpecifyKAGParser = L"KAGParser クラスのオブジェクトを指定してください";
const tjs_char* TVPKAGMalformedSaveData = L"栞データが異常です。データが破損している可能性があります";
const tjs_char* TVPLabelOrScriptInMacro = L"ラベルや iscript はマクロ中に記述できません";
const tjs_char* TVPUnknownMacroName = L"マクロ \"%1\" は登録されていません";
// Reports written by the DLL's parser itself (strings at 0x1801e3cf8, 0x1801e3d40, 0x1801e3da0).
const tjs_char* TVPKAGFileLineSuffix = L"\nファイル : %1   行 : %2";
const tjs_char* TVPKAGLabelMissing = L"指定ラベル[%1]は存在しないため、移動に失敗しました\nファイル : %2   行 : %3";
const tjs_char* TVPKAGNoScenarioForLabel = L"シナリオが読み込まれていないため、指定ラベル[%1]への移動に失敗しました";
// Upstream text (the DLL's own holder for this one was not read).
const tjs_char* TVPInternalError = L"内部エラーが発生しました: at %1 line %2";

namespace cm3d2::kag {
namespace {
thread_local const HostContext* g_current = nullptr;
const HostContext g_empty{};
} // namespace

HostScope::HostScope(const HostContext& context) : previous_(g_current), context_(context) { g_current = &context_; }
HostScope::~HostScope() { g_current = previous_; }
const HostContext& currentHost() { return g_current != nullptr ? *g_current : g_empty; }
} // namespace cm3d2::kag

namespace {

// Kirikiri message formatting: %1 and %2 are replaced by the arguments.
ttstr format(const tjs_char* message, const ttstr& a, const ttstr& b, const ttstr& c = ttstr()) {
    std::wstring out;
    for (const tjs_char* p = message; *p != 0; ++p) {
        if (p[0] == L'%' && p[1] == L'1') {
            out += a.c_str();
            ++p;
        } else if (p[0] == L'%' && p[1] == L'2') {
            out += b.c_str();
            ++p;
        } else if (p[0] == L'%' && p[1] == L'3') {
            out += c.c_str();
            ++p;
        } else {
            out.push_back(*p);
        }
    }
    return ttstr(out.c_str());
}

class WholeTextStream final : public iTJSTextReadStream {
public:
    explicit WholeTextStream(std::wstring text) : text_(std::move(text)) {}
    tjs_uint TJS_INTF_METHOD Read(TJS::tTJSString& target, tjs_uint size) override {
        const std::size_t remaining = text_.size() - position_;
        const std::size_t count = (size == 0 || size > remaining) ? remaining : size;
        target = ttstr(text_.substr(position_, count).c_str());
        position_ += count;
        return static_cast<tjs_uint>(count);
    }
    void TJS_INTF_METHOD Destruct() override { delete this; }

private:
    std::wstring text_;
    std::size_t position_ = 0;
};

} // namespace

void TVPThrowExceptionMessage(const tjs_char* message) { TJS::TJS_eTJSError(format(message, ttstr(), ttstr())); }
void TVPThrowExceptionMessage(const tjs_char* message, const ttstr& a) { TJS::TJS_eTJSError(format(message, a, ttstr())); }
void TVPThrowExceptionMessage(const tjs_char* message, const ttstr& a, const ttstr& b) { TJS::TJS_eTJSError(format(message, a, b)); }
void TVPThrowExceptionMessage(const tjs_char* message, const char* file, int line) {
    std::wstring wide_file;
    for (const char* p = file; p != nullptr && *p != 0; ++p) wide_file.push_back(static_cast<unsigned char>(*p));
    TJS::TJS_eTJSError(format(message, ttstr(wide_file.c_str()), ttstr(std::to_wstring(line).c_str())));
}

// Parser log lines ("Scenario loaded : ...") go to the script console's normal output; errors the parser reports itself
// (a missing file in a call/jump, a missing label) go to the exception output (kag_console_probe on the original).
void TVPAddLog(const ttstr& line, bool) {
    void* script = cm3d2::kag::currentHost().script;
    if (script != nullptr) cm3d2::internal::tjsLogMessage(script, line.c_str());
}

void TVPKagReport(const tjs_char* tag, const tjs_char* message, const ttstr& a, const ttstr& b, const ttstr& c) {
    void* script = cm3d2::kag::currentHost().script;
    if (script == nullptr) return;
    std::wstring text;
    if (tag != nullptr) text = std::wstring(L"[") + tag + L"]";
    text += format(message, a, b, c).c_str();
    cm3d2::internal::tjsReportException(script, text);
}

void TVPExecuteExpression(const ttstr& expression, iTJSDispatch2* context, tTJSVariant* result) {
    TJS::tTJS* engine = cm3d2::kag::currentHost().engine;
    if (engine == nullptr) TJS::TJS_eTJSError(TJS::ttstr(L"KAG: no TJS engine"));
    // The original parser evaluates [if]/[elsif]/[iscript]-style expressions without console output: a bad expression only
    // surfaces as the thrown error (checked against kag_console_probe, case if_badexp).
    TJS::iTJSConsoleOutput* console = engine->GetConsoleOutput();
    engine->SetConsoleOutput(nullptr);
    try {
        engine->EvalExpression(expression, result, context);
    } catch (...) {
        engine->SetConsoleOutput(console);
        throw;
    }
    engine->SetConsoleOutput(console);
}

ttstr TVPExtractStorageName(const ttstr& path) {
    const std::wstring text = path.c_str();
    const std::size_t at = text.find_last_of(L"/>");
    return ttstr(at == std::wstring::npos ? text.c_str() : text.c_str() + at + 1);
}

iTJSTextReadStream* TVPCreateTextStreamForRead(const ttstr& name, const ttstr&) {
    const auto& host = cm3d2::kag::currentHost();
    std::unique_ptr<cm3d2::internal::IFile> file(
        host.file_system == nullptr ? nullptr : cm3d2::internal::fileSystemOpenWide(host.file_system, name.c_str()));
    if (!file || !file->valid()) TVPThrowExceptionMessage(TVPKAGFileNotFound, name);
    std::wstring text;
    std::wstring failure;
    if (!cm3d2::internal::tjsDecodeFileText(file.get(), text, failure)) TVPThrowExceptionMessage(TVPKAGFileNotTextRead, name);
    return new WholeTextStream(std::move(text));
}

void TVPAddCompactEventHook(tTVPCompactEventCallbackIntf*) {}
