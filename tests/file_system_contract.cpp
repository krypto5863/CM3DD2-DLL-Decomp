#include "cm3d2/file_system_api.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

// Behaviour checked here was read from the FileSystemWindows pseudocode and confirmed against the
// original DLL on the same scratch tree (see tools/differential/README.md, winfs_probe).
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write(const std::filesystem::path& path, const char* text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

std::string content(CM3D2_FileSystemHandle* file_system, const char* name) {
    CM3D2_FileHandle file = nullptr;
    if (DLL_FileSystem_GetFile(file_system, name, &file) != 1) {
        return "<missing>";
    }
    std::string text(DLL_File_GetSize(&file), '\0');
    DLL_File_Read(&file, text.data(), text.size());
    DLL_File_CloseFile(&file);
    return text;
}

} // namespace

static int run() {
    namespace fs = std::filesystem;
    const auto unique = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = fs::temp_directory_path() / ("cm3d2-reconstruction-" + unique);
    std::error_code cleanup_error;
    write(root / "A.txt", "root-A");
    write(root / "Sub" / "b.TXT", "sub-b");
    write(root / "Other" / "b.txt", "other-b");
    write(root / "Sub" / "dup.txt", "sub-dup");
    write(root / "dup.txt", "root-dup");
    write(root / "Sub" / "Deep" / "c.txt", "deep-c");

    CM3D2_FileSystemHandle file_system{};
    DLL_FileSystem_CreateFileSystemWindows(&file_system);
    require(file_system.kind == CM3D2_FILE_SYSTEM_WINDOWS, "file system kind mismatch");
    require(DLL_FileSystem_IsValid(&file_system) == 0, "a fresh file system has no folders");
    require(DLL_FileSystem_GetInterfaceFileSystemWidePointer(&file_system) != nullptr,
        "wide interface pointer should be available");
    require(DLL_FileSystem_SetBaseDirectory(&file_system, "relative") == 0,
        "a base directory needs a drive separator");

    const auto folder = root.u8string();
    require(DLL_FileSystem_AddFolder(&file_system, (folder + "\\missing").c_str()) == 0,
        "a missing folder must not be added");
    require(DLL_FileSystem_IsValid(&file_system) == 0, "a failed add leaves the file system invalid");
    require(DLL_FileSystem_AddFolder(&file_system, folder.c_str()) == 1, "folder add failed");
    require(DLL_FileSystem_IsValid(&file_system) == 1, "file system should be valid after an add");
    require(DLL_FileSystem_AddFolder(&file_system, folder.c_str()) == 0, "duplicate folder");
    require(DLL_FileSystem_AddFolder(&file_system, (folder + "/").c_str()) == 0, "duplicate folder (slash)");

    // Bare names resolve case-insensitively through the flattened scan; the first file found wins.
    require(content(&file_system, "a.txt") == "root-A", "bare name lookup");
    require(content(&file_system, "A.TXT") == "root-A", "lookup is case-insensitive");
    require(content(&file_system, "a.txt ") == "root-A", "trailing spaces are trimmed");
    require(content(&file_system, "\\a.txt") == "root-A", "a leading separator is ignored");
    require(content(&file_system, "b.txt") == "other-b", "scan order decides between duplicates");
    require(content(&file_system, "dup.txt") == "root-dup", "root file wins over a subfolder file");
    require(content(&file_system, "c.txt") == "deep-c", "nested files are flattened");
    // Names with folders do not resolve in the default mode.
    require(DLL_FileSystem_IsExistentFile(&file_system, "sub\\b.txt") == 0, "folder names do not resolve");
    require(DLL_FileSystem_IsExistentFile(&file_system, "nope.txt") == 0, "unknown file");

    // Lists: sorted, unique, lowercase leaf names; modes below 2 would list sub-folders (none exist).
    {
        CM3D2_FileSystemList names{};
        DLL_FileSystem_CreateList(&file_system, "", 3, &names);
        require(names.count == 4, "all flattened names are listed once");
        char text[64]{};
        require(DLL_FileSystem_AtList(&names, 0, text, sizeof(text)) == 5 && std::string(text) == "a.txt", "first sorted name");
        require(DLL_FileSystem_AtList(&names, 3, text, 4) == 3 && std::string(text) == "dup", "AtList truncates to the buffer");
        require(DLL_FileSystem_AtList(&names, 4, text, sizeof(text)) == 0, "index past the end");
        DLL_FileSystem_DeleteList(&names);
        CM3D2_FileSystemList folders{};
        DLL_FileSystem_CreateList(&file_system, "", 1, &folders);
        require(folders.count == 0, "no sub-folder entries in default mode");
        DLL_FileSystem_DeleteList(&folders);
        CM3D2_FileSystemList by_extension{};
        DLL_FileSystem_CreateExtensionList(&file_system, "txt", &by_extension);
        require(by_extension.count == 4, "extension filter");
        DLL_FileSystem_DeleteList(&by_extension);
    }

    // Cache: serialize and read it back into a new file system. Only root files keep their
    // folder, like the original; files from sub-folders lose the relative folder in the cache.
    {
        require(DLL_FileSystem_AddAutoPath(&file_system, "cached/path") == 1, "auto path for the cache");
        const auto cache = (root / "cache.bin").u8string();
        DLL_FileSystem_CasheSerialize(&file_system, cache.c_str());
        CM3D2_FileSystemHandle loaded{};
        DLL_FileSystem_CreateFileSystemWindows(&loaded);
        require(DLL_FileSystem_CasheDeserialize(&loaded, cache.c_str()) == 1, "cache load failed");
        require(DLL_FileSystem_IsValid(&loaded) == 1, "a loaded cache has folders");
        require(content(&loaded, "a.txt") == "root-A", "root file after cache load");
        require(content(&loaded, "dup.txt") == "root-dup", "root file after cache load (dup)");
        require(content(&loaded, "c.txt") == "<missing>", "files from sub-folders are not found after a cache load");
        require(DLL_FileSystem_AddAutoPath(&loaded, "cached\\path") == 0, "auto path restored from the cache");
        DLL_FileSystem_DeleteFileSystem(&loaded);
        CM3D2_FileSystemHandle missing{};
        DLL_FileSystem_CreateFileSystemWindows(&missing);
        require(DLL_FileSystem_CasheDeserialize(&missing, (root / "no-such-cache.bin").u8string().c_str()) == 0,
            "a missing cache file fails");
        DLL_FileSystem_DeleteFileSystem(&missing);
    }

    // Windows AddArchive and AddAutoPath share one registry.
    require(DLL_FileSystem_AddAutoPath(&file_system, "zzz") == 1, "auto path add");
    require(DLL_FileSystem_AddArchive(&file_system, "ZZZ/") == 0, "auto path duplicate through AddArchive");

    // FileSystemArchive never reads loose files and is invalid until an archive is mounted.
    CM3D2_FileSystemHandle archive_system{};
    DLL_FileSystem_CreateFileSystemArchive(&archive_system);
    require(DLL_FileSystem_IsValid(&archive_system) == 0, "an archive file system starts empty");
    require(DLL_FileSystem_IsExistentFile(&archive_system, "a.txt") == 0, "no loose files in an archive system");
    DLL_FileSystem_DeleteFileSystem(&archive_system);

    DLL_FileSystem_DeleteFileSystem(&file_system);
    fs::remove_all(root, cleanup_error);
    return 0;
}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::printf("FAILED: %s\n", error.what());
        return 1;
    }
}
