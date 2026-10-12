// Drives list-style exports of the ORIGINAL (or reconstructed) Windows file system.
// Usage: winlist_probe.exe <dll> <scratch root>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
struct FsHandle { void* object; int32_t kind; int32_t reserved; };
struct ListHandle { int32_t count; int32_t reserved; void* data; };
typedef void(__cdecl* FnCreate)(FsHandle*);
typedef uint8_t(__cdecl* FnStr)(FsHandle*, const char*);
typedef void(__cdecl* FnList)(FsHandle*, const char*, int32_t, ListHandle*);
typedef void(__cdecl* FnExtList)(FsHandle*, const char*, ListHandle*);
typedef void(__cdecl* FnNameMap)(FsHandle*, const wchar_t*, ListHandle*);
typedef uint32_t(__cdecl* FnAt)(ListHandle*, int32_t, char*, int32_t);
typedef void(__cdecl* FnDel)(ListHandle*);
static HMODULE m;
template<class T> T fn(const char* n){ return reinterpret_cast<T>(GetProcAddress(m,n)); }
static void dump(const char* title, ListHandle& l, FnAt at, FnDel del) {
  printf("%s count=%d\n", title, l.count);
  for (int i = 0; i < l.count + 1; ++i) { char b[512] = {}; uint32_t n = at(&l, i, b, sizeof(b)); printf("  [%d] n=%u '%s'\n", i, n, b); }
  char tiny[4] = {'x','x','x','x'}; uint32_t n = at(&l, 0, tiny, 4); printf("  tiny n=%u '%.4s'\n", n, tiny);
  del(&l);
}
int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  auto create = fn<FnCreate>("DLL_FileSystem_CreateFileSystemWindows");
  auto addf = fn<FnStr>("DLL_FileSystem_AddFolder");
  auto list = fn<FnList>("DLL_FileSystem_CreateList");
  auto ext = fn<FnExtList>("DLL_FileSystem_CreateExtensionList");
  auto nmap = fn<FnNameMap>("DLL_FileSystem_GetNameMapLastArchive");
  auto at = fn<FnAt>("DLL_FileSystem_AtList");
  auto del = fn<FnDel>("DLL_FileSystem_DeleteList");
  FsHandle fs{}; create(&fs);
  printf("addfolder=%d\n", addf(&fs, argv[2]));
  struct Q { const char* name; int mode; };
  for (Q q : {Q{"",3},Q{"",2},Q{"*",2},Q{"",1},Q{"",0},Q{"sub",3},Q{"sub\\",2}}) {
    ListHandle l{}; list(&fs, q.name, q.mode, &l); char t[64]; sprintf_s(t, "list('%s',%d)", q.name, q.mode); dump(t, l, at, del);
  }
  for (const char* e : {"*","txt","TXT","menu","","noext"}) { ListHandle l{}; ext(&fs, e, &l); char t[64]; sprintf_s(t, "ext('%s')", e); dump(t, l, at, del); }
  for (const wchar_t* w : {(const wchar_t*)nullptr, L"", L"x.txt", L"x.menu", L"x.TXT"}) { ListHandle l{}; nmap(&fs, w, &l); printf("namemap(%ls)\n", w ? w : L"(null)"); dump("  map", l, at, del); }
  return 0;
}
