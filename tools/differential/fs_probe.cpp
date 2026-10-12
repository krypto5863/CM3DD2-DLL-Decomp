// Probes the ORIGINAL file-system archive exports on scratch copies only.
// Usage: fs_probe.exe <dll> <archive copy> <non-archive file> [names file, UTF-8, one per line]
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct FsHandle { void* object; int32_t kind; int32_t reserved; };
typedef void(__cdecl* FnCreate)(FsHandle*);
typedef uint8_t(__cdecl* FnAdd)(FsHandle*, const char*);
typedef uint8_t(__cdecl* FnExists)(FsHandle*, const char*);
typedef uint8_t(__cdecl* FnGetFile)(FsHandle*, const char*, void**);
typedef uint64_t(__cdecl* FnSize)(void**);
typedef uint64_t(__cdecl* FnRead)(void**, void*, uint64_t);
typedef void(__cdecl* FnClose)(void**);

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE m = LoadLibraryA(argv[1]);
    auto create = reinterpret_cast<FnCreate>(GetProcAddress(m, "DLL_FileSystem_CreateFileSystemArchive"));
    auto add = reinterpret_cast<FnAdd>(GetProcAddress(m, "DLL_FileSystem_AddArchive"));
    auto exists = reinterpret_cast<FnExists>(GetProcAddress(m, "DLL_FileSystem_IsExistentFile"));
    auto get = reinterpret_cast<FnGetFile>(GetProcAddress(m, "DLL_FileSystem_GetFile"));
    auto size = reinterpret_cast<FnSize>(GetProcAddress(m, "DLL_File_GetSize"));
    auto read = reinterpret_cast<FnRead>(GetProcAddress(m, "DLL_File_Read"));
    auto close = reinterpret_cast<FnClose>(GetProcAddress(m, "DLL_File_CloseFile"));

    FsHandle fs{};
    create(&fs);
    std::printf("add(missing)=%d\n", add(&fs, "arcs/does_not_exist.arc"));
    std::printf("add(non-archive)=%d\n", add(&fs, argv[3]));
    std::printf("add(archive)=%d\n", add(&fs, argv[2]));
    std::vector<std::string> list = {"edit_bg.nei", "EDIT_BG.NEI", "Edit_Bg.Nei", "csv/edit_bg.nei", "nope.nei", "edit_bg", "edit_bg.nei "};
    if (argc > 4) {
        list.clear();
        std::ifstream in(argv[4], std::ios::binary);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) list.push_back(line);
        }
    }
    for (const auto& entry : list) {
        const char* name = entry.c_str();
        void* file = nullptr;
        const uint8_t ok = get(&fs, name, &file);
        uint64_t n = 0, sum = 0;
        if (ok && file) {
            n = size(&file);
            std::vector<unsigned char> data(static_cast<size_t>(n));
            read(&file, data.data(), n);
            for (unsigned char c : data) sum = sum * 131 + c;
            close(&file);
        }
        std::printf("name[%s] exists=%d get=%d size=%llu sum=%llu\n", name, exists(&fs, name), ok,
            static_cast<unsigned long long>(n), static_cast<unsigned long long>(sum));
    }
    return 0;
}
