// Cache serialize / deserialize through the ORIGINAL or the reconstructed DLL.
// Usage: cache_probe.exe <dll> ser <scratch root> <cache file>
//        cache_probe.exe <dll> de <scratch root> <cache file> <names file>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
struct FsHandle { void* object; int32_t kind; int32_t reserved; };
struct ListHandle { int32_t count; int32_t reserved; void* data; };
typedef void(__cdecl* FnCreate)(FsHandle*);
typedef uint8_t(__cdecl* FnStr)(FsHandle*, const char*);
typedef void(__cdecl* FnStrV)(FsHandle*, const char*);
typedef uint8_t(__cdecl* FnVoid)(FsHandle*);
typedef uint8_t(__cdecl* FnGetFile)(FsHandle*, const char*, void**);
typedef uint64_t(__cdecl* FnSize)(void**);
typedef uint64_t(__cdecl* FnRead)(void**, void*, uint64_t);
typedef void(__cdecl* FnClose)(void**);
typedef void(__cdecl* FnList)(FsHandle*, const char*, int32_t, ListHandle*);
typedef void(__cdecl* FnNameMap)(FsHandle*, const wchar_t*, ListHandle*);
typedef uint32_t(__cdecl* FnAt)(ListHandle*, int32_t, char*, int32_t);
typedef void(__cdecl* FnDel)(ListHandle*);
static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  std::string mode = argv[2], root = argv[3], cache = argv[4];
  auto create = fn<FnCreate>("DLL_FileSystem_CreateFileSystemWindows");
  auto setbase = fn<FnStr>("DLL_FileSystem_SetBaseDirectory");
  auto addf = fn<FnStr>("DLL_FileSystem_AddFolder");
  auto addap = fn<FnStr>("DLL_FileSystem_AddAutoPath");
  auto valid = fn<FnVoid>("DLL_FileSystem_IsValid");
  auto exists = fn<FnStr>("DLL_FileSystem_IsExistentFile");
  auto get = fn<FnGetFile>("DLL_FileSystem_GetFile");
  auto size = fn<FnSize>("DLL_File_GetSize");
  auto rd = fn<FnRead>("DLL_File_Read");
  auto cl = fn<FnClose>("DLL_File_CloseFile");
  auto list = fn<FnList>("DLL_FileSystem_CreateList");
  auto nmap = fn<FnNameMap>("DLL_FileSystem_GetNameMapLastArchive");
  auto at = fn<FnAt>("DLL_FileSystem_AtList");
  auto del = fn<FnDel>("DLL_FileSystem_DeleteList");
  auto ser = fn<FnStrV>("DLL_FileSystem_CasheSerialize");
  auto deser = fn<FnStr>("DLL_FileSystem_CasheDeserialize");
  FsHandle fs{}; create(&fs);
  if (mode == "ser") {
    std::string parent = root.substr(0, root.find_last_of('\\'));
    printf("setbase=%d\n", setbase(&fs, parent.c_str()));
    printf("addfolder=%d\n", addf(&fs, root.c_str()));
    printf("autopath=%d\n", addap(&fs, "zzz/yy"));
    ser(&fs, cache.c_str());
    return 0;
  }
  printf("deserialize=%d\n", deser(&fs, cache.c_str()));
  printf("valid=%d\n", valid(&fs));
  std::vector<std::string> names; std::ifstream in(argv[5]);
  for (std::string l; std::getline(in, l);) { if (!l.empty() && l.back() == '\r') l.pop_back(); if (!l.empty()) names.push_back(l); }
  for (const auto& n : names) {
    void* f = nullptr; uint8_t ok = get(&fs, n.c_str(), &f); std::string c;
    if (ok && f) { uint64_t s = size(&f); std::vector<char> b((size_t)s + 1, 0); rd(&f, b.data(), s); c.assign(b.data(), (size_t)s); cl(&f); }
    printf("name[%s] exists=%d get=%d content=[%s]\n", n.c_str(), exists(&fs, n.c_str()), ok, c.c_str());
  }
  ListHandle l{}; list(&fs, "", 3, &l); printf("list count=%d\n", l.count);
  for (int i = 0; i < l.count; ++i) { char b[512] = {}; at(&l, i, b, sizeof(b)); printf("  %s\n", b); } del(&l);
  ListHandle k{}; nmap(&fs, nullptr, &k); printf("namemap count=%d\n", k.count); del(&k);
  printf("addfolder(again)=%d autopath(dup)=%d autopath(new)=%d\n", addf(&fs, root.c_str()), addap(&fs, "ZZZ\\yy"), addap(&fs, "other"));
  return 0;
}
