#pragma once
#include "cm3d2/file_api.h"
#include "cm3d2/tjs_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DLL_KAG_* (50 exports, ordinals 99..148): the KAG scenario interpreter (KagScript over BasicKagScript) on top of
 * the embedded KAGParser and a TjsScript. Handles are pointers to the script object passed by address, like the
 * other groups. Strings are UTF-8. Getters that fill a buffer truncate to capacity - 1 and always terminate.
 * See docs/kag-reconstruction.md for what is verified.
 */
typedef void* CM3D2_KagHandle;
typedef struct CM3D2_KagTagSupport CM3D2_KagTagSupport;   /* argument of tag callbacks */
typedef struct CM3D2_KagScript CM3D2_KagScript;           /* the script object the callbacks receive */
typedef struct CM3D2_KagSerializeData CM3D2_KagSerializeData;

typedef uint8_t (CM3D2_CALL* CM3D2_KagTagFunction)(CM3D2_KagTagSupport* tag);
typedef void (CM3D2_CALL* CM3D2_KagReplaceFunction)(const CM3D2_KagScript* script, const char* text, char* out, int32_t capacity);
typedef void (CM3D2_CALL* CM3D2_KagTextFunction)(const CM3D2_KagScript* script, const char* first, const char* second);
typedef void (CM3D2_CALL* CM3D2_KagReturnFunction)(const CM3D2_KagScript* script, const char* text, int32_t depth);
typedef void (CM3D2_CALL* CM3D2_KagLabelFunction)(CM3D2_KagScript* script, uint8_t seen);

/* Lifetime and execution */
CM3D2_API void CM3D2_CALL DLL_KAG_Create(CM3D2_TjsScriptHandle* script, CM3D2_KagHandle* out_kag);
CM3D2_API void CM3D2_CALL DLL_KAG_Delete(CM3D2_KagHandle* kag);
CM3D2_API void CM3D2_CALL DLL_KAG_SetFileSystem(CM3D2_KagHandle* kag, void* file_system_wide_interface);
CM3D2_API void CM3D2_CALL DLL_KAG_LoadScenario(CM3D2_KagHandle* kag, const char* storage);
CM3D2_API void CM3D2_CALL DLL_KAG_LoadScenarioString(CM3D2_KagHandle* kag, const char* text);
CM3D2_API void CM3D2_CALL DLL_KAG_GoToLabel(CM3D2_KagHandle* kag, const char* label);
CM3D2_API int32_t CM3D2_CALL DLL_KAG_Exec(CM3D2_KagHandle* kag);
CM3D2_API uint32_t CM3D2_CALL DLL_KAG_IsExecScenario(CM3D2_KagHandle* kag);
CM3D2_API void CM3D2_CALL DLL_KAG_TextClear(CM3D2_KagHandle* kag);
CM3D2_API void CM3D2_CALL DLL_KAG_ClearCallStack(CM3D2_KagHandle* kag);
CM3D2_API void CM3D2_CALL DLL_KAG_ClearScnearioCache(CM3D2_KagHandle* unused);

/* State */
CM3D2_API void CM3D2_CALL DLL_KAG_GetText(CM3D2_KagHandle* kag, char* destination, int32_t capacity);
CM3D2_API void CM3D2_CALL DLL_KAG_GetStoragename(CM3D2_KagHandle* kag, char* destination, int32_t capacity);
CM3D2_API void CM3D2_CALL DLL_KAG_GetCurrentLabel(CM3D2_KagHandle* kag, char* destination, int32_t capacity);
CM3D2_API int32_t CM3D2_CALL DLL_KAG_GetCurrentLine(CM3D2_KagHandle* kag);
CM3D2_API int32_t CM3D2_CALL DLL_KAG_GetCallStackDepth(CM3D2_KagHandle* kag);
CM3D2_API int32_t CM3D2_CALL DLL_KAG_GetRCallStackDepth(CM3D2_KagHandle* kag);
CM3D2_API uint64_t CM3D2_CALL DLL_KAG_GetUniqueID(CM3D2_KagHandle* kag);
CM3D2_API uint64_t CM3D2_CALL DLL_KAG_GetUniqueIDFromBasicKagScriptPtr(const CM3D2_KagScript* script);
CM3D2_API void CM3D2_CALL DLL_KAG_SetTagCallEnabled(CM3D2_KagHandle* kag, uint8_t enabled);
CM3D2_API uint32_t CM3D2_CALL DLL_KAG_GetTagCallEnabled(CM3D2_KagHandle* kag);
CM3D2_API void CM3D2_CALL DLL_KAG_SetTagReturnEnabled(CM3D2_KagHandle* kag, uint8_t enabled);
CM3D2_API uint32_t CM3D2_CALL DLL_KAG_GetTagReturnEnabled(CM3D2_KagHandle* kag);

/* Records and save data */
CM3D2_API void CM3D2_CALL DLL_KAG_AddFinishFile(CM3D2_KagHandle* kag, const char* storage);
CM3D2_API uint32_t CM3D2_CALL DLL_KAG_GetFinishFile(CM3D2_KagHandle* kag, const char* storage);
CM3D2_API void CM3D2_CALL DLL_KAG_AddRecordLabel(CM3D2_KagHandle* kag, const char* storage, const char* label);
CM3D2_API uint32_t CM3D2_CALL DLL_KAG_GetRecordLabel(CM3D2_KagHandle* kag, const char* storage, const char* label);
CM3D2_API CM3D2_KagSerializeData* CM3D2_CALL DLL_KAG_CreateSerializeData(CM3D2_KagHandle* kag);
CM3D2_API int32_t CM3D2_CALL DLL_KAG_GetSerializeDataSize(const CM3D2_KagSerializeData* data);
CM3D2_API void CM3D2_CALL DLL_KAG_WriteSerializeData(void* destination, const CM3D2_KagSerializeData* data);
CM3D2_API void CM3D2_CALL DLL_KAG_ReleaseSerializeData(CM3D2_KagSerializeData* data);
CM3D2_API int64_t CM3D2_CALL DLL_KAG_Deserialize(CM3D2_KagHandle* kag, const void* data);

/* Tags */
CM3D2_API uint64_t CM3D2_CALL DLL_KAG_GetKey(void* unused, const char* name);
CM3D2_API uint64_t CM3D2_CALL DLL_KAG_AddTag(CM3D2_KagHandle* kag, const char* name, CM3D2_KagTagFunction function);
CM3D2_API uint32_t CM3D2_CALL DLL_KAG_RemoveTag(CM3D2_KagHandle* kag, const char* name);
CM3D2_API uint32_t CM3D2_CALL DLL_KAG_TAG_IsValid(CM3D2_KagTagSupport* tag, const char* name);
CM3D2_API void CM3D2_CALL DLL_KAG_TAG_GetTagProperty(CM3D2_KagTagSupport* tag, const char* name, void** variant_handle);
CM3D2_API uint64_t CM3D2_CALL DLL_KAG_TAG_GetTagKey(const CM3D2_KagTagSupport* tag);
CM3D2_API uint64_t CM3D2_CALL DLL_KAG_TAG_GetClassUniqueid(const CM3D2_KagTagSupport* tag);
CM3D2_API void CM3D2_CALL DLL_KAG_TAG_CreateTagList(CM3D2_KagTagSupport* tag);
CM3D2_API int64_t CM3D2_CALL DLL_KAG_TAG_GetListCount(const CM3D2_KagTagSupport* tag);
CM3D2_API void CM3D2_CALL DLL_KAG_TAG_GetListNameAt(const CM3D2_KagTagSupport* tag, int32_t index, char* destination, int32_t capacity);
CM3D2_API void CM3D2_CALL DLL_KAG_TAG_GetListElmAt(const CM3D2_KagTagSupport* tag, int32_t index, char* destination, int32_t capacity);

/* Callbacks */
CM3D2_API void CM3D2_CALL DLL_KAG_SetReplaceFunction(CM3D2_KagHandle* kag, CM3D2_KagReplaceFunction function);
CM3D2_API void CM3D2_CALL DLL_KAG_SetOnScenarioLoadFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function);
CM3D2_API void CM3D2_CALL DLL_KAG_SetOnCallFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function);
CM3D2_API void CM3D2_CALL DLL_KAG_SetOnRCallFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function);
CM3D2_API void CM3D2_CALL DLL_KAG_SetOnReturnFunction(CM3D2_KagHandle* kag, CM3D2_KagReturnFunction function);
CM3D2_API void CM3D2_CALL DLL_KAG_SetOnRReturnFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function);
CM3D2_API void CM3D2_CALL DLL_KAG_SetOnLabelCallBackFunction(CM3D2_KagHandle* kag, CM3D2_KagLabelFunction function);

#ifdef __cplusplus
}
#endif
