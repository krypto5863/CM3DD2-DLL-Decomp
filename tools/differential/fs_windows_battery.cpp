// Windows file system differential battery. Builds an owned temp tree, then prints deterministic transcripts of lookups, lists and
// cache files so the ORIGINAL and the reconstructed DLL can be diffed. Also exchanges cache files between DLLs.
// Usage: fs_windows_battery.exe <dll> <work dir> [load-cache <path>]   (work dir is created/overwritten by this tool)
//   Without "load-cache": builds the tree, runs the battery, serializes the cache to <work>/cache_<tag>.bin and prints its size/FNV.
//   With "load-cache <path>": mounts the tree, deserializes <path> into a fresh file system and runs the lookups.
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct Fs { void* object; int kind; int reserved; };
struct FileH { void* object; };
struct ListH { int32_t count; int32_t reserved; void* strings; };
static HMODULE m;
template <class T> T fn(const char* n) { T p = reinterpret_cast<T>(GetProcAddress(m, n)); if (!p) { std::printf("missing %s\n", n); std::exit(3); } return p; }

static void writeFile(const std::string& path, const std::string& text) {
  std::ofstream f(path, std::ios::binary); f << text;
}
static void mkdirs(const std::string& path) { CreateDirectoryA(path.c_str(), nullptr); }

static uint64_t fnv(const std::vector<char>& b) {
  uint64_t h = 1469598103934665603ull; for (char c : b) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull; return h;
}
static std::vector<char> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary); return std::vector<char>((std::istreambuf_iterator<char>(f)), {});
}

static void showList(Fs& fs, const char* label, const char* name, int mode) {
  ListH list{};
  fn<void(__cdecl*)(Fs*, const char*, int, ListH*)>("DLL_FileSystem_CreateList")(&fs, name, mode, &list);
  std::printf("CreateList(%s, mode %d) count=%d\n", label, mode, list.count);
  for (int i = 0; i < list.count; ++i) {
    char b[512] = {};
    fn<uint32_t(__cdecl*)(ListH*, int, char*, int)>("DLL_FileSystem_AtList")(&list, i, b, sizeof b);
    std::printf("  [%d] %s\n", i, b);
  }
  fn<void(__cdecl*)(ListH*)>("DLL_FileSystem_DeleteList")(&list);
}
static void showExt(Fs& fs, const char* ext) {
  ListH list{};
  fn<void(__cdecl*)(Fs*, const char*, ListH*)>("DLL_FileSystem_CreateExtensionList")(&fs, ext, &list);
  std::printf("CreateExtensionList(%s) count=%d\n", ext, list.count);
  for (int i = 0; i < list.count; ++i) {
    char b[512] = {};
    fn<uint32_t(__cdecl*)(ListH*, int, char*, int)>("DLL_FileSystem_AtList")(&list, i, b, sizeof b);
    std::printf("  [%d] %s\n", i, b);
  }
  fn<void(__cdecl*)(ListH*)>("DLL_FileSystem_DeleteList")(&list);
}
static void lookups(Fs& fs) {
  const char* names[] = {"a.txt", "b.txt", "c.txt", "dup.txt", "DUP.TXT", "sub\\b.txt", "sub/b.txt", "sub\\deep\\c.txt", "x.menu", "X.Menu", "only2.txt",
                         "missing.txt", "", "sub", "dup.txt ", "tail.dat", "noext", "UPPER.TXT", "upper.txt", "\\a.txt"};
  for (const char* n : names) {
    const uint32_t exists = fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, n);
    FileH f{};
    const uint32_t got = fn<uint32_t(__cdecl*)(Fs*, const char*, FileH*)>("DLL_FileSystem_GetFile")(&fs, n, &f);
    std::string text = "<none>";
    if (got) {
      char b[64] = {}; const uint64_t sz = fn<uint64_t(__cdecl*)(FileH*, void*, uint64_t)>("DLL_File_Read")(&f, b, 63);
      text.assign(b, static_cast<size_t>(sz));
      fn<void(__cdecl*)(FileH*)>("DLL_File_CloseFile")(&f);
    }
    std::printf("lookup '%s': exists=%u get=%u content=%s\n", n, exists & 0xff, got & 0xff, text.c_str());
  }
}

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  if (!m) return 3;
  std::string work = argv[2];
  const std::string r1 = work + "\\r1", r2 = work + "\\r2", r3 = work + "\\r3empty";
  const bool loadMode = argc >= 5 && std::strcmp(argv[3], "load-cache") == 0;
  if (!loadMode) {
    mkdirs(work); mkdirs(r1); mkdirs(r1 + "\\Sub"); mkdirs(r1 + "\\Sub\\Deep"); mkdirs(r1 + "\\.works1"); mkdirs(r1 + "\\.ignoreX"); mkdirs(r1 + "\\keep.d");
    mkdirs(r2); mkdirs(r2 + "\\Sub"); mkdirs(r3);
    writeFile(r1 + "\\a.txt", "r1-a"); writeFile(r1 + "\\dup.txt", "r1-dup"); writeFile(r1 + "\\Sub\\b.txt", "r1-sub-b");
    writeFile(r1 + "\\Sub\\dup.txt", "r1-sub-dup"); writeFile(r1 + "\\Sub\\Deep\\c.txt", "r1-deep-c"); writeFile(r1 + "\\.works1\\hidden.txt", "hidden");
    writeFile(r1 + "\\.ignoreX\\hidden2.txt", "hidden2"); writeFile(r1 + "\\keep.d\\kept.txt", "kept"); writeFile(r1 + "\\UPPER.TXT", "r1-upper");
    writeFile(r1 + "\\tail.dat", "r1-tail"); writeFile(r1 + "\\noext", "r1-noext"); writeFile(r1 + "\\x.menu", "r1-menu");
    writeFile(r2 + "\\dup.txt", "r2-dup"); writeFile(r2 + "\\only2.txt", "r2-only"); writeFile(r2 + "\\Sub\\b.txt", "r2-sub-b"); writeFile(r2 + "\\upper.txt", "r2-upper");
    writeFile(r2 + "\\y.MENU", "r2-menu");
  }
  Fs fs{};
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemWindows")(&fs);
  auto add = fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddFolder");
  if (loadMode) {
    std::printf("Deserialize=%u\n", fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_CasheDeserialize")(&fs, argv[4]) & 0xff);
  } else {
    std::printf("AddFolder r1=%u r1again=%u r2=%u r3empty=%u missing=%u\n", add(&fs, r1.c_str()) & 0xff, add(&fs, r1.c_str()) & 0xff, add(&fs, r2.c_str()) & 0xff,
      add(&fs, r3.c_str()) & 0xff, add(&fs, (work + "\\nope").c_str()) & 0xff);
  }
  std::printf("IsValid=%u\n", fn<uint32_t(__cdecl*)(Fs*)>("DLL_FileSystem_IsValid")(&fs) & 0xff);
  lookups(fs);
  for (int mode = 0; mode <= 3; ++mode) {
    showList(fs, "", "", mode); showList(fs, "*", "*", mode); showList(fs, "sub", "sub", mode); showList(fs, "Sub\\Deep", "Sub\\Deep", mode);
  }
  showExt(fs, "txt"); showExt(fs, "menu"); showExt(fs, "*"); showExt(fs, "dat"); showExt(fs, "noext"); showExt(fs, "");
  {
    ListH list{};
    fn<void(__cdecl*)(Fs*, const char*, ListH*)>("DLL_FileSystem_GetNameMapLastArchive")(&fs, nullptr, &list);
    std::printf("GetNameMapLastArchive count=%d\n", list.count);
    for (int i = 0; i < list.count; ++i) { char b[512] = {}; fn<uint32_t(__cdecl*)(ListH*, int, char*, int)>("DLL_FileSystem_AtList")(&list, i, b, sizeof b); std::printf("  [%d] %s\n", i, b); }
    fn<void(__cdecl*)(ListH*)>("DLL_FileSystem_DeleteList")(&list);
  }
  if (!loadMode) {
    const std::string cache = work + "\\cache_" + (argc > 3 ? argv[3] : "x") + ".bin";
    fn<void(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_CasheSerialize")(&fs, cache.c_str());
    const auto bytes = slurp(cache);
    std::printf("cache %s size=%zu fnv=%016llx\n", cache.c_str(), bytes.size(), static_cast<unsigned long long>(fnv(bytes)));
  }
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&fs);
  return 0;
}
