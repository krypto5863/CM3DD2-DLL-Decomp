#include "cm3d2/file_system_api.h"
#include "cm3d2/file_api.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string readName(CM3D2_FileSystemHandle& fs, const char* name) {
    CM3D2_FileHandle file{};
    if (DLL_FileSystem_GetFile(&fs, name, &file) != 1) return "<missing>";
    char text[32]{};
    const auto size = DLL_File_Read(&file, text, sizeof(text) - 1);
    DLL_File_CloseFile(&file);
    return std::string(text, static_cast<std::size_t>(size));
}
}

// Source basis: FUN_18007e8c0 walks the registered roots from the LAST list node to the first (it reads the node's previous
// link, offset +8, starting at the sentinel). A name present in several folders therefore resolves in the folder that was
// registered last. Confirmed against the original DLL with two folders holding the same file name (2026-10-12).
int main() {
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("cm3d2-folder-order-" + std::to_string(stamp));
    try {
        fs::create_directories(root / "a");
        fs::create_directories(root / "b");
        { std::ofstream file(root / "a" / "same.txt", std::ios::binary); file << "from-a"; }
        { std::ofstream file(root / "b" / "same.txt", std::ios::binary); file << "from-b"; }
        { std::ofstream file(root / "a" / "only-a.txt", std::ios::binary); file << "only-a"; }
        const auto a = (root / "a").u8string();
        const auto b = (root / "b").u8string();

        CM3D2_FileSystemHandle ab{};
        DLL_FileSystem_CreateFileSystemWindows(&ab);
        require(DLL_FileSystem_AddFolder(&ab, a.c_str()) == 1 && DLL_FileSystem_AddFolder(&ab, b.c_str()) == 1, "add a then b");
        require(readName(ab, "same.txt") == "from-b", "last registered folder wins (a then b)");
        require(readName(ab, "only-a.txt") == "only-a", "names unique to an earlier folder still resolve");
        DLL_FileSystem_DeleteFileSystem(&ab);

        CM3D2_FileSystemHandle ba{};
        DLL_FileSystem_CreateFileSystemWindows(&ba);
        require(DLL_FileSystem_AddFolder(&ba, b.c_str()) == 1 && DLL_FileSystem_AddFolder(&ba, a.c_str()) == 1, "add b then a");
        require(readName(ba, "same.txt") == "from-a", "last registered folder wins (b then a)");
        DLL_FileSystem_DeleteFileSystem(&ba);

        fs::remove_all(root);
        std::puts("file_system_folder_order_contract ok");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
