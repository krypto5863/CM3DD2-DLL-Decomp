#pragma once

#include "cm3d2/file_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The handle is a pointer to the database object, passed by address like the other handles. */
typedef void* CM3D2_MenuDataBaseHandle;

/* categories and colors are EnumData handles (string to number maps). */
CM3D2_API void CM3D2_CALL DLL_MenuDataBase_New(
    CM3D2_MenuDataBaseHandle* out_database, void* categories, void* colors);
CM3D2_API void CM3D2_CALL DLL_MenuDataBase_Delete(CM3D2_MenuDataBaseHandle* database);
/* file_system_wide_interface comes from DLL_FileSystem_GetInterfaceFileSystemWidePointer. */
CM3D2_API void CM3D2_CALL DLL_MenuDataBase_StartAnalysis(
    CM3D2_MenuDataBaseHandle* database, void* file_system_wide_interface);
CM3D2_API uint32_t CM3D2_CALL DLL_MenuDataBase_JobFinished(CM3D2_MenuDataBaseHandle* database);
CM3D2_API int64_t CM3D2_CALL DLL_MenuDataBase_Size(CM3D2_MenuDataBaseHandle* database);
CM3D2_API void CM3D2_CALL DLL_MenuDataBase_SetIndex(CM3D2_MenuDataBaseHandle* database, int32_t index);
CM3D2_API const char* CM3D2_CALL DLL_MenuDataBase_GetFileNameToNativeHash(
    CM3D2_MenuDataBaseHandle* database, uint64_t native_hash);
CM3D2_API const char* CM3D2_CALL DLL_MenuDataBase_GetParentFileNameToNativeHash(
    CM3D2_MenuDataBaseHandle* database, uint64_t native_hash);

/* Getters on the record chosen with DLL_MenuDataBase_SetIndex. Strings are borrowed. */
CM3D2_API uint32_t CM3D2_CALL DLL_MenuParse_GetHeaderCheck(CM3D2_MenuDataBaseHandle* database);
CM3D2_API int32_t CM3D2_CALL DLL_MenuParse_GetVersion(CM3D2_MenuDataBaseHandle* database);
CM3D2_API uint64_t CM3D2_CALL DLL_MenuParse_GetNativeHash(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetSrcFileName(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetItemName(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetCategoryName(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetInfoText(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetMenuName(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetItemInfoText(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetCategoryMpnText(CM3D2_MenuDataBaseHandle* database);
CM3D2_API int32_t CM3D2_CALL DLL_MenuParse_GetCategoryMpn(CM3D2_MenuDataBaseHandle* database);
CM3D2_API int32_t CM3D2_CALL DLL_MenuParse_GetColorSetMpn(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetMenuNameInColorSet(CM3D2_MenuDataBaseHandle* database);
CM3D2_API int32_t CM3D2_CALL DLL_MenuParse_GetMultiColorId(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetIconS(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetSaveItem(CM3D2_MenuDataBaseHandle* database);
CM3D2_API uint32_t CM3D2_CALL DLL_MenuParse_GetBoDelOnly(CM3D2_MenuDataBaseHandle* database);
CM3D2_API const char* CM3D2_CALL DLL_MenuParse_GetPriorityText(CM3D2_MenuDataBaseHandle* database);
CM3D2_API uint32_t CM3D2_CALL DLL_MenuParse_GetIsMan(CM3D2_MenuDataBaseHandle* database);
CM3D2_API uint32_t CM3D2_CALL DLL_MenuParse_GetIsCollabo(CM3D2_MenuDataBaseHandle* database);

#ifdef __cplusplus
}
#endif
