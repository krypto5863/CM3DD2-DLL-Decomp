#pragma once
#include "cm3d2/file_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DLL_TJS_* (0x180067870..0x1800742c0): a TjsScript object wrapping the TJS2 engine.
 * Variant arguments are raw variant pointers, the kind DLL_TJSVariant_Reference wraps.
 * The file argument of the *File exports is an interface file pointer (e.g. from
 * DLL_FileMemory_GetInterfaceFilePointer), not a handle.
 * host_context is a borrowed wide filesystem interface (from FileSystem or
 * FileSystemDelegate); it must remain alive through script deletion. Scripts'
 * storage methods own/delete each file returned by that provider. A null context
 * is supported for expression-only engines; storage then reports loader failure.
 * Explicitly invalidate objects with script finalizers while their engine,
 * globals, and callbacks are still alive. Merely clearing an owned variant can
 * leave cached references. Arbitrary finalizer execution during/after script
 * deletion is unsupported; native teardown itself has observable fatal cases.
 */
typedef void* CM3D2_TjsScriptHandle;
typedef struct CM3D2_TjsParamAccess CM3D2_TjsParamAccess;
typedef void (CM3D2_CALL* CM3D2_TjsConsoleCallback)(const char* utf8_message, uint8_t is_exception);
typedef void (CM3D2_CALL* CM3D2_TjsMemberFunction)(const CM3D2_TjsParamAccess* parameters, void* result_variant);

CM3D2_API void CM3D2_CALL DLL_TJS_CreateTjsScriptClass(void* host_context, CM3D2_TjsScriptHandle* out_script);
CM3D2_API void CM3D2_CALL DLL_TJS_CreateTjsConsoleClass(CM3D2_TjsScriptHandle* script, CM3D2_TjsConsoleCallback callback);
CM3D2_API void CM3D2_CALL DLL_TJS_DeleteTjsScriptClass(CM3D2_TjsScriptHandle* script);
CM3D2_API void CM3D2_CALL DLL_TJS_ExecScript(CM3D2_TjsScriptHandle* script, const char* utf8_source, void* result_variant);
CM3D2_API void CM3D2_CALL DLL_TJS_ExecScriptFile(CM3D2_TjsScriptHandle* script, void* interface_file, void* result_variant);
CM3D2_API void CM3D2_CALL DLL_TJS_EvalScript(CM3D2_TjsScriptHandle* script, const char* utf8_expression, void* result_variant);
CM3D2_API void CM3D2_CALL DLL_TJS_EvalScriptFile(CM3D2_TjsScriptHandle* script, void* interface_file, void* result_variant);
CM3D2_API uint64_t CM3D2_CALL DLL_TJS_AddMemberFunction(
    CM3D2_TjsScriptHandle* script, const char* utf8_name, CM3D2_TjsMemberFunction function);
CM3D2_API uint64_t CM3D2_CALL DLL_TJS_GetUniqueID(CM3D2_TjsScriptHandle* script);

CM3D2_API int32_t CM3D2_CALL DLL_TJS_ParamAccess_Size(const CM3D2_TjsParamAccess* parameters);
CM3D2_API uint64_t CM3D2_CALL DLL_TJS_ParamAccess_GetClassUniqueId(const CM3D2_TjsParamAccess* parameters);
CM3D2_API uint64_t CM3D2_CALL DLL_TJS_ParamAccess_GetNameKey(const CM3D2_TjsParamAccess* parameters);
CM3D2_API void* CM3D2_CALL DLL_TJS_ParamAccess_At(const CM3D2_TjsParamAccess* parameters, int32_t index);

#ifdef __cplusplus
}
#endif
