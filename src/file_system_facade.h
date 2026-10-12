#pragma once

#include <string>
#include <vector>

#include "file_interface.h"

namespace cm3d2::internal {

// What other groups use from a file system through the pointer that
// DLL_FileSystem_GetInterfaceFileSystemWidePointer returns (the wide interface, +0x10).

// Wide slot 0 (FUN_18007b650 / FUN_18008d6e0): opens a file by name. The caller owns the result;
// null when the name is not found.
IFile* fileSystemOpenWide(const void* wide_interface, const std::wstring& name);

// Recognizes only live reconstructed delegates; unknown pointers are rejected.
// Registered callbacks transfer ownership of their returned file to the caller.
IFile* fileSystemDelegateOpenWide(const void* wide_interface, const std::wstring& name);

// Wide slot 2 (FUN_180079fe0 / FUN_18008c410): sorted, unique names below a folder.
std::vector<std::wstring> fileSystemListWide(const void* wide_interface, const std::wstring& name, int mode);

// Virtual-base slot 4 (FUN_18006e9fc): the menu-name set in iteration order.
std::vector<std::wstring> fileSystemMenuNames(const void* wide_interface);

} // namespace cm3d2::internal
