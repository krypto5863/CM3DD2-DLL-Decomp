#include "cm3d2/csv_api.h"
#include "cm3d2/file_api.h"
#include "cm3d2/file_system_api.h"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Writes a small non-archive file so the rejection checks never depend on game data.
std::string writePlainFile() {
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    std::string path = std::string(dir) + "cm3d2_not_an_archive.txt";
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") == 0 && file != nullptr) {
        std::fputs("plain text, not a warc archive", file);
        std::fclose(file);
    }
    return path;
}

} // namespace

// Archive checks need a warc archive from the user's game data (for example
// GameData_20\csv.arc). Set CM3D2_WARC_SAMPLE to its path; without it the archive part
// is skipped. The sample is only read, never written.
int runArchiveContract() {
    CM3D2_FileSystemHandle file_system{};
    DLL_FileSystem_CreateFileSystemArchive(&file_system);
    require(file_system.object != nullptr, "archive file system creation failed");

    require(DLL_FileSystem_AddArchive(&file_system, "does_not_exist.arc") == 0,
        "missing file must not mount");
    const std::string plain = writePlainFile();
    require(DLL_FileSystem_AddArchive(&file_system, plain.c_str()) == 0,
        "non-archive file must not mount");

    const char* sample = std::getenv("CM3D2_WARC_SAMPLE");
    if (sample == nullptr || *sample == '\0') {
        std::printf("archive part skipped: set CM3D2_WARC_SAMPLE to a warc archive\n");
        DLL_FileSystem_DeleteFileSystem(&file_system);
        return 77;
    }

    require(DLL_FileSystem_AddArchive(&file_system, sample) == 1, "warc sample did not mount");
    require(DLL_FileSystem_IsExistentFile(&file_system, "edit_bg.nei") == 1,
        "member lookup failed");
    require(DLL_FileSystem_IsExistentFile(&file_system, "EDIT_BG.NEI") == 1,
        "lookup must be case-insensitive");
    require(DLL_FileSystem_IsExistentFile(&file_system, "edit_bg.nei ") == 1,
        "trailing spaces must be ignored");
    require(DLL_FileSystem_IsExistentFile(&file_system, "csv/edit_bg.nei") == 0,
        "directory prefixes must not match");
    require(DLL_FileSystem_IsExistentFile(&file_system, "no_such_member.nei") == 0,
        "unknown member must not match");

    // Decode the member through the CSV reader: archive -> file system -> carrier -> table.
    CM3D2_FileHandle member = nullptr;
    require(DLL_FileSystem_GetFile(&file_system, "edit_bg.nei", &member) == 1 && member != nullptr,
        "member retrieval failed");
    CM3D2_CsvHandle csv = DLL_CSV_CreateCsvParser();
    DLL_CSV_Open(csv, member);
    require(DLL_CSV_IsValid(csv) == 1, "archive member did not decode as a table");
    require(DLL_CSV_GetMaxCellX(csv) == 8 && DLL_CSV_GetMaxCellY(csv) == 24,
        "archive edit_bg.nei table size mismatch (DLC csv.arc: 8 x 24)");

    char cell[64] = {};
    DLL_CSV_GetCellAsString(csv, 0, 0, cell, sizeof(cell));
    wchar_t decoded[16] = {};
    MultiByteToWideChar(932, 0, cell, -1, decoded, 16);
    // The DLC table's header cell is "ID".
    const wchar_t expected[] = {L'I', L'D', 0};
    require(std::wcscmp(decoded, expected) == 0, "archive table header cell mismatch");

    DLL_CSV_DeleteCsvParser(csv);
    DLL_File_CloseFile(&member);
    DLL_FileSystem_DeleteFileSystem(&file_system);
    std::printf("ok: archive mounted, names resolved, member decoded\n");
    return 0;
}

int main() {
    try {
        return runArchiveContract();
    } catch (const std::exception& error) {
        std::printf("FAILED: %s\n", error.what());
        return 1;
    }
}
