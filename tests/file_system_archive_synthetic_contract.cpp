#include "cm3d2/file_api.h"
#include "cm3d2/file_system_api.h"
#include "support/synthetic_warc.h"
#include <cstdio>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string read(CM3D2_FileSystemHandle& fs, const char* name) {
    CM3D2_FileHandle file = nullptr;
    if (!DLL_FileSystem_GetFile(&fs, name, &file)) return "<missing>";
    char data[128]{};
    const auto length = DLL_File_Read(&file, data, sizeof(data));
    DLL_File_CloseFile(&file);
    require(length <= sizeof(data), "bounded fixture read");
    return std::string(data, static_cast<std::size_t>(length));
}
std::vector<std::string> list(CM3D2_FileSystemHandle& fs, const char* path, int mode) {
    CM3D2_FileSystemList out{};
    DLL_FileSystem_CreateList(&fs, path, mode, &out);
    std::vector<std::string> names;
    for (int i = 0; i < out.count; ++i) {
        char text[512]{};
        DLL_FileSystem_AtList(&out, i, text, sizeof(text));
        names.emplace_back(text);
    }
    DLL_FileSystem_DeleteList(&out);
    return names;
}
}

// Golden expectations corroborated by fs_archive_battery against the verified
// original using only these synthetic plain fixtures. No optional game samples.
int main() {
    std::filesystem::path owned;
    CM3D2_FileSystemHandle fs{};
    try {
        for (int i = 0; i < 100; ++i) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("cm3d2-archive-contract-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { owned = candidate; break; }
        }
        require(!owned.empty(), "fresh owned fixture directory");
        synthetic_warc::fixtures(owned);
        auto add = [&](const char* name) {
            return DLL_FileSystem_AddArchive(&fs, (owned / name).u8string().c_str());
        };
        DLL_FileSystem_CreateFileSystemArchive(&fs);
        require(!DLL_FileSystem_IsValid(&fs), "fresh archive set");
        require(!add("missing.arc") && !add("plain.txt"), "missing/plain file rejection");
        require(add("a.arc") && add("b.arc") && add("a.arc"), "mount/duplicate success");
        require(read(fs, "same.txt") == "B-root", "newest archive wins; duplicate does not reorder");
        require(read(fs, "choose.txt") == "B-other-choice", "default recursive table order");
        require(read(fs, "only-a.txt") == "A-only", "fallback to older archive");
        require(read(fs, u8"\u65e5\u672c.txt") == "A-unicode", "UTF-8 parallel table");
        require(DLL_FileSystem_AddAutoPath(&fs, "sub") && !DLL_FileSystem_AddAutoPath(&fs, "SUB/"), "normalized auto-path duplicate");
        require(read(fs, "same.txt") == "B-sub", "auto-path table overrides root table");
        require(DLL_FileSystem_AddAutoPath(&fs, "sub/inner") && DLL_FileSystem_AddAutoPath(&fs, "other"), "additional auto paths");
        require(read(fs, "choose.txt") == "B-other-choice", "newest auto-path wins within newest archive");
        require(read(fs, "inner/choose.txt") == "A-inner-choice", "qualified path relative to auto-path");
        require(read(fs, "SUB/INNER/choose.txt") == "A-inner-choice", "explicit nested path + case normalization");
        for (const std::uint8_t async : {std::uint8_t(0), std::uint8_t(1)}) {
            DLL_FileSystem_AddAutoPathForAllFolder(&fs, async);
            require(DLL_FileSystem_IsFinishedAddAutoPathJob(&fs, 0), "default mode immediate completion");
            DLL_FileSystem_ReleaseAddAutoPathJob(&fs);
        }
        require(add("c.arc") && read(fs, "same.txt") == "B-sub", "new archive without colliding name preserves fallback");
        const auto all = list(fs, "", 3);
        require(all == list(fs, "*", 3), "wildcard/empty listing equality");
        require(std::is_sorted(all.begin(), all.end()) && std::adjacent_find(all.begin(), all.end()) == all.end(), "sorted unique listing");
        require(std::find(all.begin(), all.end(), "sub\\inner\\choose.txt") != all.end(), "recursive listing contains full path");
        require(list(fs, "missing", 3).empty(), "unknown folder listing");
        require(add("empty.arc"), "valid empty archive mount");
        CM3D2_FileSystemList map{};
        DLL_FileSystem_GetNameMapLastArchive(&fs, nullptr, &map);
        require(map.count == 0, "last archive name map, not merged map");
        DLL_FileSystem_DeleteList(&map);
        require(read(fs, "same.txt") == "B-sub", "empty newest archive preserves older hits");
        DLL_FileSystem_DeleteFileSystem(&fs);

        DLL_FileSystem_CreateFileSystemArchive(&fs);
        require(DLL_FileSystem_AddAutoPath(&fs, "sub"), "auto path before mounting");
        require(add("b.arc") && add("a.arc"), "reversed mount order");
        require(read(fs, "same.txt") == "A-sub", "pre-registered auto path applied to each mount");
        DLL_FileSystem_DeleteFileSystem(&fs);
        // Only this process's atomically created fixture directory is removed.
        std::filesystem::remove_all(owned);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "archive synthetic contract failed: %s\n", e.what());
        if (fs.object) DLL_FileSystem_DeleteFileSystem(&fs);
        // Keep failed fixtures for diagnosis rather than deleting partial evidence.
        return 1;
    }
}
