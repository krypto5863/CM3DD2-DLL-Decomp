#include "cm3d2/file_api.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    const char input[] = "abcdef";
    CM3D2_FileHandle file = nullptr;

    DLL_FileMemory_CreateileMemory(&file);
    require(file != nullptr, "creation returned a null file");
    require(DLL_FileMemory_GetInterfaceFilePointer(&file) == file, "interface pointer mismatch");
    require(DLL_File_IsValid(&file) == 0, "new memory file should be invalid");
    DLL_FileMemory_SetData(&file, nullptr, 0);
    require(DLL_File_IsValid(&file) == 1, "empty copied file should be valid");
    require(DLL_File_GetSize(&file) == 0, "empty file size mismatch");
    require(DLL_File_SetSeek(&file, 10, 1) == 1 && DLL_File_GetTell(&file) == 0,
        "empty file seek should succeed and clamp");
    char sentinel = 'x';
    require(DLL_File_Read(&file, &sentinel, 1) == 0 && sentinel == 'x', "empty file read");
    DLL_FileMemory_SetData(&file, nullptr, 1);
    require(DLL_File_IsValid(&file) == 0, "null positive input is deliberately rejected");
    DLL_FileMemory_SetData(&file, input, -1);
    require(DLL_File_IsValid(&file) == 0, "negative size is deliberately rejected");
    DLL_FileMemory_SetData(&file, input, 6);
    require(DLL_File_IsValid(&file) == 1, "memory file should be valid");
    require(DLL_File_GetSize(&file) == 6, "memory file size mismatch");
    require(DLL_File_GetTell(&file) == 0, "initial position mismatch");

    char output[4]{};
    require(DLL_File_Read(&file, output, 3) == 3, "first read count mismatch");
    require(std::memcmp(output, "abc", 3) == 0, "first read data mismatch");
    require(DLL_File_GetTell(&file) == 3, "position after first read mismatch");

    require(DLL_File_SetSeek(&file, -2, 0) == 1, "relative seek failed");
    require(DLL_File_GetTell(&file) == 1, "position after seek mismatch");
    std::memset(output, 0, sizeof(output));
    require(DLL_File_Read(&file, output, 2) == 2, "second read count mismatch");
    require(std::memcmp(output, "bc", 2) == 0, "second read data mismatch");

    DLL_FileMemory_DeleteileMemory(&file);
    return 0;
}
