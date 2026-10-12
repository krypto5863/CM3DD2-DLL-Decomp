// Drives the ORIGINAL (or reconstructed) archive file system on scratch copies only.
// Usage: arch_probe.exe <dll> <archive copy 1> <names file> [archive copy 2]
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
typedef uint8_t(__cdecl* FnVoid)(FsHandle*);
typedef uint8_t(__cdecl* FnGetFile)(FsHandle*, const char*, void**);
typedef uint64_t(__cdecl* FnSize)(void**);
typedef uint64_t(__cdecl* FnRead)(void**, void*, uint64_t);
typedef void(__cdecl* FnClose)(void**);
typedef void(__cdecl* FnList)(FsHandle*, const char*, int32_t, ListHandle*);
typedef void(__cdecl* FnExtList)(FsHandle*, const char*, ListHandle*);
typedef void(__cdecl* FnNameMap)(FsHandle*, const wchar_t*, ListHandle*);
typedef uint32_t(__cdecl* FnAt)(ListHandle*, int32_t, char*, int32_t);
typedef void(__cdecl* FnDel)(ListHandle*);
static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }

static FnAt at; static FnDel del;
static void dump(const char* title, ListHandle& l) {
  uint64_t h = 1469598103934665603ULL;
  std::string first, last;
  for (int i = 0; i < l.count; ++i) {
    char b[1024] = {};
    at(&l, i, b, sizeof(b));
    for (const char* p = b; *p; ++p) { h = (h ^ (unsigned char)*p) * 1099511628211ULL; }
    h = (h ^ 10) * 1099511628211ULL;
    if (i < 4) first += std::string(b) + " | ";
    if (i >= l.count - 3) last += std::string(b) + " | ";
  }
  printf("%s count=%d hash=%llu first=[%s] last=[%s]\n", title, l.count, (unsigned long long)h, first.c_str(), last.c_str());
  del(&l);
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  auto create = fn<FnCreate>("DLL_FileSystem_CreateFileSystemArchive");
  auto add = fn<FnStr>("DLL_FileSystem_AddArchive");
  auto addap = fn<FnStr>("DLL_FileSystem_AddAutoPath");
  auto valid = fn<FnVoid>("DLL_FileSystem_IsValid");
  auto exists = fn<FnStr>("DLL_FileSystem_IsExistentFile");
  auto get = fn<FnGetFile>("DLL_FileSystem_GetFile");
  auto size = fn<FnSize>("DLL_File_GetSize");
  auto rd = fn<FnRead>("DLL_File_Read");
  auto cl = fn<FnClose>("DLL_File_CloseFile");
  auto list = fn<FnList>("DLL_FileSystem_CreateList");
  auto ext = fn<FnExtList>("DLL_FileSystem_CreateExtensionList");
  auto nmap = fn<FnNameMap>("DLL_FileSystem_GetNameMapLastArchive");
  at = fn<FnAt>("DLL_FileSystem_AtList");
  del = fn<FnDel>("DLL_FileSystem_DeleteList");
  FsHandle fs{}; create(&fs);
  printf("valid(fresh)=%d\n", valid(&fs));
  int r1 = addap(&fs, "system\\sceneedit\\preset"); printf("autopath(before archive)=%d\n", r1);
  int r2 = add(&fs, argv[2]); int r3 = add(&fs, argv[2]);
  printf("add=%d add-again=%d valid=%d\n", r2, r3, valid(&fs));
  int a1 = addap(&fs, "SYSTEM/Preset/"); printf("autopath(SYSTEM/Preset/)=%d\n", a1);
  int a2 = addap(&fs, "system\\sceneedit\\preset"); printf("autopath(dup)=%d\n", a2);
  int a3 = addap(&fs, "system\\yotogi"); printf("autopath(yotogi)=%d\n", a3);
  int a4 = addap(&fs, "nosuchfolder"); printf("autopath(missing)=%d\n", a4);
  int a5 = addap(&fs, ""); printf("autopath(empty)=%d\n", a5);
  if (argc > 4) printf("add(second)=%d\n", add(&fs, argv[4]));
  std::vector<std::string> names; std::ifstream in(argv[3]);
  for (std::string l; std::getline(in, l);) { if (!l.empty() && l.back() == '\r') l.pop_back(); if (!l.empty()) names.push_back(l); }
  for (const auto& n : names) {
    void* f = nullptr; uint8_t ok = get(&fs, n.c_str(), &f); uint64_t s = 0, sum = 0;
    if (ok && f) { s = size(&f); std::vector<unsigned char> b((size_t)s); rd(&f, b.data(), s); for (auto c : b) sum = sum * 131 + c; cl(&f); }
    printf("name[%s] exists=%d get=%d size=%llu sum=%llu\n", n.c_str(), exists(&fs, n.c_str()), ok, (unsigned long long)s, (unsigned long long)sum);
  }
  struct Q { const char* name; int mode; };
  for (Q q : {Q{"",3},Q{"",2},Q{"*",3},Q{"",1},Q{"",0},Q{"system",3},Q{"system",2},Q{"system\\sceneedit",1},Q{"SYSTEM/",0},Q{"system\\trophy\\",3},Q{"nothing",3}}) {
    ListHandle l{}; list(&fs, q.name, q.mode, &l); char t[96]; sprintf_s(t, "list('%s',%d)", q.name, q.mode); dump(t, l);
  }
  for (const char* e : {"*","tex","menu","","asset_bg_shader"}) { ListHandle l{}; ext(&fs, e, &l); char t[96]; sprintf_s(t, "ext('%s')", e); dump(t, l); }
  for (const wchar_t* w : {(const wchar_t*)nullptr, L"", L"x.tex", L"x.menu"}) { ListHandle l{}; nmap(&fs, w, &l); printf("namemap(%ls) ", w ? w : L"(null)"); dump("map", l); }
  return 0;
}
