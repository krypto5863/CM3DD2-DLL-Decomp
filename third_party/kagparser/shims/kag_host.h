#pragma once
// Host services the embedded Kirikiri KAGParser expects (normally ScriptMgnIntf, MsgIntf, StorageImpl, TextStream,
// EventIntf, DebugIntf). Implemented in src/kag_host.cpp. Declarations only.
#include <string>

#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsString.h"
#include "tjsVariant.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsNative.h"
#include "tjsHashSearch.h"
#include "tjsObject.h"

using TJS::ttstr;
using TJS::tTJSVariant;
using TJS::iTJSDispatch2;
using TJS::tjs_char;
using TJS::tjs_int;
using TJS::tjs_uint;
using TJS::tjs_uint32;

// Message texts recovered from the DLL (the project notes (not part of this repository)).
extern const tjs_char* TVPKAGNoLine;
extern const tjs_char* TVPKAGCannotOmmitFirstLabelName;
extern const tjs_char* TVPKAGFileNotFound;
extern const tjs_char* TVPKAGFileNotTextRead;
extern const tjs_char* TVPKAGLabelNotFound;
extern const tjs_char* TVPKAGInlineScriptNotEnd;
extern const tjs_char* TVPKAGSyntaxError;
extern const tjs_char* TVPKAGMacroEntityNotAvailable;
extern const tjs_char* TVPKAGCallStackUnderflow;
extern const tjs_char* TVPKAGReturnLostSync;
extern const tjs_char* TVPKAGSpecifyKAGParser;
extern const tjs_char* TVPKAGMalformedSaveData;
extern const tjs_char* TVPLabelOrScriptInMacro;
extern const tjs_char* TVPUnknownMacroName;
extern const tjs_char* TVPInternalError;

[[noreturn]] void TVPThrowExceptionMessage(const tjs_char* message);
[[noreturn]] void TVPThrowExceptionMessage(const tjs_char* message, const ttstr& a);
[[noreturn]] void TVPThrowExceptionMessage(const tjs_char* message, const ttstr& a, const ttstr& b);
[[noreturn]] void TVPThrowExceptionMessage(const tjs_char* message, const char* file, int line);

void TVPAddLog(const ttstr& line, bool imm = false);
// Console exception output: "[tag]" (when tag is not null) + message with %1..%3 replaced (cm3d2 local addition).
void TVPKagReport(const tjs_char* tag, const tjs_char* message, const ttstr& a = ttstr(), const ttstr& b = ttstr(), const ttstr& c = ttstr());
extern const tjs_char* TVPKAGFileLineSuffix;
extern const tjs_char* TVPKAGLabelMissing;
extern const tjs_char* TVPKAGNoScenarioForLabel;
void TVPClearScnearioCache();  // defined in KAGParser.cpp
void TVPExecuteExpression(const ttstr& expression, iTJSDispatch2* context, tTJSVariant* result);
ttstr TVPExtractStorageName(const ttstr& path);

// Text input: the scenario source is read through the engine's iTJSTextReadStream (tjs.h).
iTJSTextReadStream* TVPCreateTextStreamForRead(const ttstr& name, const ttstr& modestr);

enum { TVP_COMPACT_LEVEL_IDLE = 5, TVP_COMPACT_LEVEL_DEACTIVATE = 10, TVP_COMPACT_LEVEL_MINIMIZE = 15, TVP_COMPACT_LEVEL_MAX = 100 };
class tTVPCompactEventCallbackIntf {
public:
    virtual void OnCompact(tjs_int level) = 0;
};
void TVPAddCompactEventHook(tTVPCompactEventCallbackIntf* callback);
