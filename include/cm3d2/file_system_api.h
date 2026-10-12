#pragma once

#include "cm3d2/file_api.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The handle DLL_FileSystem_CreateList and friends fill in: an element count and a pointer to a
   vector of UTF-8 strings that DLL_FileSystem_AtList reads and DLL_FileSystem_DeleteList frees. */
typedef struct CM3D2_FileSystemList {
    int32_t count;
    int32_t reserved;
    void* strings;
} CM3D2_FileSystemList;

enum CM3D2_FileSystemKind {
    CM3D2_FILE_SYSTEM_WINDOWS = 0,
    CM3D2_FILE_SYSTEM_ARCHIVE = 1
};

/* Matches the observed pointer + 32-bit discriminator wrapper layout. */
typedef struct CM3D2_FileSystemHandle {
    void* object;
    int32_t kind;
    int32_t reserved;
} CM3D2_FileSystemHandle;

CM3D2_API void CM3D2_CALL DLL_FileSystem_CreateFileSystemWindows(
    CM3D2_FileSystemHandle* out_file_system);
CM3D2_API void CM3D2_CALL DLL_FileSystem_CreateFileSystemArchive(
    CM3D2_FileSystemHandle* out_file_system);
CM3D2_API void CM3D2_CALL DLL_FileSystem_DeleteFileSystem(
    CM3D2_FileSystemHandle* file_system);

CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_SetBaseDirectory(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_AddFolder(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path);

CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_AddArchive(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_AddAutoPath(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path);
CM3D2_API void CM3D2_CALL DLL_FileSystem_AddAutoPathForAllFolder(
    CM3D2_FileSystemHandle* file_system,
    uint8_t multithread_processing); /* managed: bool multithread_processing */
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_IsFinishedAddAutoPathJob(
    CM3D2_FileSystemHandle* file_system,
    uint8_t wait);
CM3D2_API void CM3D2_CALL DLL_FileSystem_ReleaseAddAutoPathJob(
    CM3D2_FileSystemHandle* file_system);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_IsValid(
    CM3D2_FileSystemHandle* file_system);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_IsExistentFile(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_name);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_GetFile(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_name,
    CM3D2_FileHandle* out_file);
CM3D2_API void* CM3D2_CALL DLL_FileSystem_GetInterfaceFileSystemWidePointer(
    CM3D2_FileSystemHandle* file_system);

CM3D2_API void CM3D2_CALL DLL_FileSystem_CreateList(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_name,
    int32_t mode,
    CM3D2_FileSystemList* out_list);
CM3D2_API void CM3D2_CALL DLL_FileSystem_CreateExtensionList(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_extension,
    CM3D2_FileSystemList* out_list);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_AtList(
    CM3D2_FileSystemList* list,
    int32_t index,
    char* buffer,
    int32_t buffer_size);
CM3D2_API void CM3D2_CALL DLL_FileSystem_DeleteList(CM3D2_FileSystemList* list);
CM3D2_API void CM3D2_CALL DLL_FileSystem_GetNameMapLastArchive(
    CM3D2_FileSystemHandle* file_system,
    const wchar_t* file_name,
    CM3D2_FileSystemList* out_list);

CM3D2_API void CM3D2_CALL DLL_FileSystem_AddPatchDecryptPreferredSearchDirectory(
    const char* cp932_path);
CM3D2_API void CM3D2_CALL DLL_FileSystem_ClearPatchDecryptPreferredSearchDirectory(void);
CM3D2_API void CM3D2_CALL DLL_FileSystem_CasheSerialize(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_CasheDeserialize(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path);
CM3D2_API uint32_t CM3D2_CALL DLL_FileSystem_TestFlag(void);

#ifdef __cplusplus
}
#endif
