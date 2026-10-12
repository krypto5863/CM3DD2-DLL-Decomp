#include <Windows.h>

#include "cm3d2/csv_api.h"
#include "cm3d2/file_api.h"

#include <cstdio>
#include <vector>

// Scratch probe: opens a real .nei through the reconstructed file + CSV APIs and
// prints every cell as hex of its raw cp932 bytes. Not part of the repo.
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    wchar_t path[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, path, MAX_PATH);

    CM3D2_FileHandle file = nullptr;
    DLL_Win32File_Create(&file);
    if (!DLL_Win32File_Open(&file, path)) { std::fprintf(stderr, "open failed\n"); return 1; }

    CM3D2_CsvHandle csv = DLL_CSV_CreateCsvParser();
    DLL_CSV_Open(csv, file);
    std::printf("valid=%d maxX=%d maxY=%d\n", DLL_CSV_IsValid(csv),
        DLL_CSV_GetMaxCellX(csv), DLL_CSV_GetMaxCellY(csv));

    for (int y = 0; y <= DLL_CSV_GetMaxCellY(csv); ++y) {
        for (int x = 0; x <= DLL_CSV_GetMaxCellX(csv); ++x) {
            const int32_t size = DLL_CSV_GetDataSizeString(csv, x, y);
            std::printf("%d %d %d ", x, y, size);
            if (size > 0) {
                std::vector<char> buffer(static_cast<size_t>(size) + 1, 0);
                DLL_CSV_GetCellAsString(csv, x, y, buffer.data(), static_cast<int32_t>(buffer.size()));
                for (int32_t i = 0; i < size - 1; ++i) std::printf("%02x", static_cast<unsigned char>(buffer[i]));
            }
            std::printf("\n");
        }
    }
    DLL_CSV_DeleteCsvParser(csv);
    DLL_Win32File_Delete(&file);
    return 0;
}
