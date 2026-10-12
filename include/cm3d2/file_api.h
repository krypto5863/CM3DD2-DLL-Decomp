#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(CM3D2_RECONSTRUCTION_BUILD)
#    define CM3D2_API __declspec(dllexport)
#  else
#    define CM3D2_API __declspec(dllimport)
#  endif
#  define CM3D2_CALL __cdecl
#else
#  define CM3D2_API
#  define CM3D2_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* CM3D2_FileHandle;

/* Recovered named exports. These names intentionally retain upstream typos. */
CM3D2_API void CM3D2_CALL DLL_Win32File_Create(CM3D2_FileHandle* out_file);
CM3D2_API void CM3D2_CALL DLL_Win32File_Delete(CM3D2_FileHandle* file);
CM3D2_API uint32_t CM3D2_CALL DLL_Win32File_Open(
    CM3D2_FileHandle* file,
    const wchar_t* path);

CM3D2_API uint32_t CM3D2_CALL DLL_File_IsValid(CM3D2_FileHandle* file);
CM3D2_API uint32_t CM3D2_CALL DLL_File_SetSeek(
    CM3D2_FileHandle* file,
    int64_t offset,
    uint8_t absolute);
CM3D2_API uint64_t CM3D2_CALL DLL_File_Read(
    CM3D2_FileHandle* file,
    void* destination,
    uint64_t byte_count);
CM3D2_API uint64_t CM3D2_CALL DLL_File_GetTell(CM3D2_FileHandle* file);
CM3D2_API uint64_t CM3D2_CALL DLL_File_GetSize(CM3D2_FileHandle* file);
CM3D2_API void CM3D2_CALL DLL_File_CloseFile(CM3D2_FileHandle* file);
CM3D2_API void CM3D2_CALL DLL_File_ReleaseInstanceCount(void);

CM3D2_API void CM3D2_CALL DLL_FileMemory_CreateileMemory(CM3D2_FileHandle* out_file);
CM3D2_API CM3D2_FileHandle CM3D2_CALL DLL_FileMemory_GetInterfaceFilePointer(
    CM3D2_FileHandle* file);
CM3D2_API void CM3D2_CALL DLL_FileMemory_SetData(
    CM3D2_FileHandle* file,
    const void* data,
    int32_t byte_count);
CM3D2_API void CM3D2_CALL DLL_FileMemory_DeleteileMemory(CM3D2_FileHandle* file);

#ifdef __cplusplus
}
#endif
