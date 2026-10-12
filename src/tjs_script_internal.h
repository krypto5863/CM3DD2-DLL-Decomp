#pragma once

#include <string>

namespace TJS {
class tTJS;
class tTJSVariant;
}

namespace cm3d2::internal {

class IFile;

// Seam from the TJS wrapper (src/tjs_api.cpp) for the KAG group. `script_pointer` is the value of a
// TjsScript handle (what DLL_KAG_Create receives through its handle argument).
struct TjsScriptAccess {
    TJS::tTJS* engine = nullptr;
    void* host_context = nullptr;  // borrowed wide file system interface
};

bool tjsScriptAccess(void* script_pointer, TjsScriptAccess& out);
// The TjsScript file loader (BOM detection, UTF-16LE / UTF-8 / code page 932).
bool tjsDecodeFileText(IFile* file, std::wstring& text, std::wstring& failure);

// Evaluates an expression; false when it failed (the error text was printed to the script console).
bool tjsRunExpression(void* script_pointer, const std::wstring& text, TJS::tTJSVariant* result);
// Prints a message through the script console's exception output.
void tjsReportException(void* script_pointer, const std::wstring& message);
// Prints a message through the script console's normal (non-exception) output.
void tjsLogMessage(void* script_pointer, const std::wstring& message);

} // namespace cm3d2::internal
