#include "cm3d2/file_system_api.h"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<unsigned char> readAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::uint64_t fnv(const std::vector<unsigned char>& data) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto byte : data) {
        hash = (hash ^ byte) * 0x100000001b3ULL;
    }
    return hash;
}

std::string copyToScratch(const std::string& source, const std::string& directory) {
    const auto name = source.substr(source.find_last_of("\\/") + 1);
    const std::string target = directory + "\\" + name;
    require(CopyFileA(source.c_str(), target.c_str(), FALSE) != 0, "copy to scratch failed");
    return target;
}

} // namespace

// Warp (DLC patch) archives are rewritten on disk by AddArchive, so this test works on scratch copies. It needs a patch
// archive ("..._2.arc" with the "warp" magic) and its base archive from the user's game data: set CM3D2_WARP_PATCH and
// CM3D2_WARP_BASE to their paths (both are only read). Optionally CM3D2_WARP_EXPECT_FNV (hex) is the FNV-1a 64 of the
// converted patch as the original DLL writes it; without the variables the test is skipped.
int main() {
    const char* patch_source = std::getenv("CM3D2_WARP_PATCH");
    const char* base_source = std::getenv("CM3D2_WARP_BASE");
    if (patch_source == nullptr || base_source == nullptr) {
        std::puts("warp_archive_contract: CM3D2_WARP_PATCH / CM3D2_WARP_BASE not set, skipped");
        return 77;
    }
    try {
        char temp[MAX_PATH];
        GetTempPathA(MAX_PATH, temp);
        const std::string root = std::string(temp) + "cm3d2_warp_contract";
        const std::string with_base = root + "\\with_base";
        const std::string alone = root + "\\alone";
        CreateDirectoryA(root.c_str(), nullptr);
        CreateDirectoryA(with_base.c_str(), nullptr);
        CreateDirectoryA(alone.c_str(), nullptr);

        // 1. A patch without its base mounted is refused and left alone.
        {
            const auto patch = copyToScratch(patch_source, alone);
            const auto before = readAll(patch);
            require(before.size() > 4 && std::memcmp(before.data(), "warp", 4) == 0, "sample is not a warp archive");
            CM3D2_FileSystemHandle fs{};
            DLL_FileSystem_CreateFileSystemArchive(&fs);
            require(DLL_FileSystem_AddArchive(&fs, patch.c_str()) == 0, "patch without base should fail");
            require(readAll(patch) == before, "a refused patch must stay unchanged");
            DLL_FileSystem_DeleteFileSystem(&fs);
        }

        // 2. With the base mounted first the patch is converted to a warc archive in place and mounted.
        {
            const auto base = copyToScratch(base_source, with_base);
            const auto patch = copyToScratch(patch_source, with_base);
            CM3D2_FileSystemHandle fs{};
            DLL_FileSystem_CreateFileSystemArchive(&fs);
            require(DLL_FileSystem_AddArchive(&fs, base.c_str()) == 1, "base archive should mount");
            require(DLL_FileSystem_AddArchive(&fs, patch.c_str()) == 1, "patch should convert and mount");
            const auto converted = readAll(patch);
            static const unsigned char warc[8] = {0x77, 0x61, 0x72, 0x63, 0xff, 0xaa, 0x45, 0xf1};
            require(converted.size() > 8 && std::memcmp(converted.data(), warc, 8) == 0, "patch should now be a warc archive");
            if (const char* expect = std::getenv("CM3D2_WARP_EXPECT_FNV")) {
                require(fnv(converted) == std::strtoull(expect, nullptr, 16), "converted patch differs from the original DLL's");
            }
            // Mounting the converted file again is accepted (it is a plain warc now).
            require(DLL_FileSystem_AddArchive(&fs, patch.c_str()) == 1, "converted patch should mount again");
            DLL_FileSystem_DeleteFileSystem(&fs);
        }

        // 3. A base in another directory is found through the preferred search directories.
        {
            const std::string base_dir = root + "\\base_dir";
            const std::string patch_dir = root + "\\patch_dir";
            CreateDirectoryA(base_dir.c_str(), nullptr);
            CreateDirectoryA(patch_dir.c_str(), nullptr);
            const auto base = copyToScratch(base_source, base_dir);
            const auto patch = copyToScratch(patch_source, patch_dir);
            CM3D2_FileSystemHandle fs{};
            DLL_FileSystem_CreateFileSystemArchive(&fs);
            require(DLL_FileSystem_AddArchive(&fs, base.c_str()) == 1, "base archive should mount");
            require(DLL_FileSystem_AddArchive(&fs, patch.c_str()) == 0, "base in another directory is not found");
            DLL_FileSystem_AddPatchDecryptPreferredSearchDirectory(base_dir.c_str());
            require(DLL_FileSystem_AddArchive(&fs, patch.c_str()) == 1, "preferred directory should provide the base");
            DLL_FileSystem_ClearPatchDecryptPreferredSearchDirectory();
            DLL_FileSystem_DeleteFileSystem(&fs);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "warp_archive_contract failed: %s\n", error.what());
        return 1;
    }
    std::puts("warp_archive_contract passed");
    return 0;
}
