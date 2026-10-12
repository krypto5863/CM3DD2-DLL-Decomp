#pragma once

#include <string>

#include "file_interface.h"

namespace cm3d2::internal {

// FileNormalWindows (vtable 0x1801dcf50): a buffered "rb" file, opened by FUN_180074880.
// Returns null when the file cannot be opened. The caller owns the object.
IFile* openFileNormalWindows(const std::wstring& path);

// FUN_18008feb0 constructs the ordinary FileMemory (vtable 0x1801dc980),
// transferring an already decoded new[] buffer through slot 9. Takes ownership
// even on allocation failure. A zero-size new[] buffer remains a valid file.
// Does not adjust the global file-instance count; the exported wrapper does so.
IFile* makeFileMemoryOwned(std::byte* data, std::uint64_t size);

// DLL_FileSystem_GetFile counts each file it hands out (_DAT_18021dc54); DLL_File_CloseFile
// takes the count back.
void noteFileInstanceCreated();

} // namespace cm3d2::internal
