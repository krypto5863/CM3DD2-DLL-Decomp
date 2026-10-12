#pragma once
#include "cm3d2/file_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DLL_FileSystemDelegate_* (0x18006dff0..0x18006e310). The delegate is a wide
 * file-system object: its GetFile and IsExistentFile slots forward to callbacks
 * the host registers, and a null callback or null path reports failure.
 * The export DDLL_FileSystemDelegate_GetFile keeps the upstream double-D
 * prefix (ordinal 1).
 */
typedef void* CM3D2_FileSystemDelegateHandle;

typedef uint8_t (CM3D2_CALL *CM3D2_IsExistentFileCallBack)(const wchar_t* path);
typedef CM3D2_FileHandle (CM3D2_CALL *CM3D2_GetFileWideCallBack)(const wchar_t* path);

CM3D2_API void CM3D2_CALL DLL_FileSystemDelegate_Create(
    CM3D2_FileSystemDelegateHandle* out_delegate);
CM3D2_API void CM3D2_CALL DLL_FileSystemDelegate_Delete(
    CM3D2_FileSystemDelegateHandle* delegate);
CM3D2_API void* CM3D2_CALL DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(
    CM3D2_FileSystemDelegateHandle* delegate);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystemDelegate_IsValid(
    CM3D2_FileSystemDelegateHandle* delegate);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystemDelegate_IsExistentFile(
    CM3D2_FileSystemDelegateHandle* delegate,
    const wchar_t* path);
CM3D2_API void CM3D2_CALL DLL_FileSystemDelegate_RegisterIsExistentFileCallBack(
    CM3D2_FileSystemDelegateHandle* delegate,
    CM3D2_IsExistentFileCallBack callback);
CM3D2_API void CM3D2_CALL DLL_FileSystemDelegate_RegisterGetFileWideCallBack(
    CM3D2_FileSystemDelegateHandle* delegate,
    CM3D2_GetFileWideCallBack callback);
CM3D2_API uint32_t CM3D2_CALL DDLL_FileSystemDelegate_GetFile(
    CM3D2_FileSystemDelegateHandle* delegate,
    const wchar_t* path,
    CM3D2_FileHandle* out_file);

#ifdef __cplusplus
}
#endif
