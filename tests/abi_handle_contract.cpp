#include "cm3d2/file_system_api.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>

// The managed game passes pointers to small slots. The original FileSystem Create exports write exactly 12 bytes (object +
// kind) and the list exports write count and pointer but not the middle 4 bytes; writing more would corrupt neighbouring
// fields of the caller (found with tools/differential/abi_canary_probe.cpp against the original DLL).
int main() {
    unsigned char slot[64];
    int failures = 0;
    const auto untouchedFrom = [&](int first, const char* what) {
        for (int i = first; i < 64; ++i) {
            if (slot[i] != 0xCC) {
                std::fprintf(stderr, "%s wrote byte %d\n", what, i);
                ++failures;
                return;
            }
        }
    };

    std::memset(slot, 0xCC, sizeof(slot));
    DLL_FileSystem_CreateFileSystemWindows(reinterpret_cast<CM3D2_FileSystemHandle*>(slot));
    untouchedFrom(12, "CreateFileSystemWindows");
    auto* handle = reinterpret_cast<CM3D2_FileSystemHandle*>(slot);
    CM3D2_FileSystemHandle copy = *handle;
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);

    unsigned char list_slot[64];
    std::memset(list_slot, 0xCC, sizeof(list_slot));
    DLL_FileSystem_CreateList(&copy, "", 2, reinterpret_cast<CM3D2_FileSystemList*>(list_slot));
    for (int i = 4; i < 8; ++i) {
        if (list_slot[i] != 0xCC) {
            std::fprintf(stderr, "CreateList wrote byte %d\n", i);
            ++failures;
            break;
        }
    }
    for (int i = 16; i < 64; ++i) {
        if (list_slot[i] != 0xCC) {
            std::fprintf(stderr, "CreateList wrote byte %d\n", i);
            ++failures;
            break;
        }
    }
    DLL_FileSystem_DeleteList(reinterpret_cast<CM3D2_FileSystemList*>(list_slot));
    DLL_FileSystem_DeleteFileSystem(&copy);

    std::memset(slot, 0xCC, sizeof(slot));
    DLL_FileSystem_CreateFileSystemArchive(reinterpret_cast<CM3D2_FileSystemHandle*>(slot));
    untouchedFrom(12, "CreateFileSystemArchive");
    CM3D2_FileSystemHandle archive = *reinterpret_cast<CM3D2_FileSystemHandle*>(slot);
    DLL_FileSystem_DeleteFileSystem(&archive);

    if (failures != 0) {
        return 1;
    }
    std::puts("abi_handle_contract passed");
    return 0;
}
