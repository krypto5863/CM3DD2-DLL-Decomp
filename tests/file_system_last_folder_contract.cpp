#include "cm3d2/file_system_api.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::string> lastFolderNames(CM3D2_FileSystemHandle& fs) {
    CM3D2_FileSystemList list{};
    DLL_FileSystem_GetNameMapLastArchive(&fs, nullptr, &list);
    std::vector<std::string> names;
    for (int i = 0; i < list.count; ++i) {
        char text[64]{};
        DLL_FileSystem_AtList(&list, i, text, sizeof(text));
        names.emplace_back(text);
    }
    DLL_FileSystem_DeleteList(&list);
    return names;
}
}

// Source basis: FUN_180078580 clears +0xb8 only after a new valid folder is
// registered (machine 0x1800788c8..0x1800788cf). Lookups retain earlier roots.
int main() {
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("cm3d2-last-folder-" + std::to_string(stamp));
    CM3D2_FileSystemHandle handle{};
    try {
        fs::create_directories(root / "first");
        fs::create_directories(root / "second");
        fs::create_directories(root / "empty");
        { std::ofstream file(root / "first" / "first.txt"); file << "first"; }
        { std::ofstream file(root / "second" / "second.txt"); file << "second"; }
        DLL_FileSystem_CreateFileSystemWindows(&handle);
        const auto first = (root / "first").u8string();
        const auto second = (root / "second").u8string();
        require(DLL_FileSystem_AddFolder(&handle, first.c_str()) == 1, "first add");
        require(lastFolderNames(handle) == std::vector<std::string>{"first.txt"}, "first map");
        require(DLL_FileSystem_AddFolder(&handle, second.c_str()) == 1, "second add");
        require(lastFolderNames(handle) == std::vector<std::string>{"second.txt"}, "map must only contain latest folder");
        require(DLL_FileSystem_IsExistentFile(&handle, "first.txt") == 1, "earlier root remains searchable");
        require(DLL_FileSystem_AddFolder(&handle, first.c_str()) == 0, "duplicate add");
        require(lastFolderNames(handle) == std::vector<std::string>{"second.txt"}, "duplicate preserves map");
        require(DLL_FileSystem_AddFolder(&handle, (root / "missing").u8string().c_str()) == 0, "missing add");
        require(lastFolderNames(handle) == std::vector<std::string>{"second.txt"}, "failed add preserves map");
        require(DLL_FileSystem_AddFolder(&handle, (root / "empty").u8string().c_str()) == 1, "empty add");
        require(lastFolderNames(handle).empty(), "empty folder clears map");
        require(DLL_FileSystem_IsExistentFile(&handle, "first.txt") == 1, "empty add preserves lookup");
        DLL_FileSystem_DeleteFileSystem(&handle);
        handle.object = nullptr;
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        if (handle.object) DLL_FileSystem_DeleteFileSystem(&handle);
        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::printf("FAILED: %s\n", error.what());
        return 1;
    }
}
