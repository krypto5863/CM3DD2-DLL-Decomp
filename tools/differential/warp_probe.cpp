// Warp (DLC patch) archives through the ORIGINAL or the reconstructed DLL. WORK ON COPIES ONLY: the original rewrites archive
// files on disk when it opens them (tmp file + rename).
// Usage: warp_probe.exe <dll> <op>... [-- <member name>...]
// ops, executed in order: add:<path> (AddArchive, prints the result), base:<dir> (SetBaseDirectory), pref:<dir>
// (AddPatchDecryptPreferredSearchDirectory), clearpref (ClearPatchDecryptPreferredSearchDirectory). A bare path is add:<path>.
// For every member name after "--" it prints whether the file exists and a hash of its bytes.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct FsHandle { void* object; int32_t kind; int32_t reserved; };
static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 3) return 2;
  m = LoadLibraryA(argv[1]);
  FsHandle fs{};
  fn<void(__cdecl*)(FsHandle*)>("DLL_FileSystem_CreateFileSystemArchive")(&fs);
  int i = 2;
  for (; i < argc && std::strcmp(argv[i], "--") != 0; ++i) {
    const std::string op = argv[i];
    if (op.rfind("base:", 0) == 0) {
      std::printf("base=%d\n", fn<uint8_t(__cdecl*)(FsHandle*, const char*)>("DLL_FileSystem_SetBaseDirectory")(&fs, op.c_str() + 5));
    } else if (op.rfind("pref:", 0) == 0) {
      fn<void(__cdecl*)(const char*)>("DLL_FileSystem_AddPatchDecryptPreferredSearchDirectory")(op.c_str() + 5);
      std::printf("pref\n");
    } else if (op == "clearpref") {
      fn<void(__cdecl*)()>("DLL_FileSystem_ClearPatchDecryptPreferredSearchDirectory")();
      std::printf("clearpref\n");
    } else {
      const char* path = op.rfind("add:", 0) == 0 ? op.c_str() + 4 : op.c_str();
      std::printf("add=%d\n", fn<uint8_t(__cdecl*)(FsHandle*, const char*)>("DLL_FileSystem_AddArchive")(&fs, path));
    }
  }
  for (++i; i < argc; ++i) {
    void* file = nullptr;
    const uint8_t ok = fn<uint8_t(__cdecl*)(FsHandle*, const char*, void**)>("DLL_FileSystem_GetFile")(&fs, argv[i], &file);
    std::printf("%s exists=%d get=%d", argv[i], fn<uint8_t(__cdecl*)(FsHandle*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, argv[i]), ok);
    if (ok && file) {
      const uint64_t n = fn<uint64_t(__cdecl*)(void**)>("DLL_File_GetSize")(&file);
      std::vector<unsigned char> data(static_cast<size_t>(n));
      const uint64_t got = fn<uint64_t(__cdecl*)(void**, void*, uint64_t)>("DLL_File_Read")(&file, data.data(), n);
      uint64_t h = 0xcbf29ce484222325ull;
      for (uint64_t k = 0; k < got; ++k) h = (h ^ data[k]) * 0x100000001b3ull;
      std::printf(" size=%llu hash=%016llx", static_cast<unsigned long long>(n), static_cast<unsigned long long>(h));
      fn<void(__cdecl*)(void**)>("DLL_File_CloseFile")(&file);
    }
    std::printf("\n");
  }
  return 0;
}
