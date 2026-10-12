// Menu database through the ORIGINAL or the reconstructed DLL.
// Usage: menu_probe.exe <dll> win <folder>            (loose folder)
//        menu_probe.exe <dll> arc <archive copy>      (archive file system)
#include <Windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
struct FsHandle { void* object; int32_t kind; int32_t reserved; };
static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
typedef void(__cdecl* FnCreate)(FsHandle*);
typedef uint8_t(__cdecl* FnStr)(FsHandle*, const char*);
typedef void* (__cdecl* FnWide)(FsHandle*);
typedef void* (__cdecl* FnEnumNew)();
typedef uint8_t(__cdecl* FnEnumAdd)(void*, int32_t, const char*);
typedef void(__cdecl* FnDbNew)(void**, void*, void*);
typedef void(__cdecl* FnDbDel)(void**);
typedef void(__cdecl* FnDbStart)(void**, void*);
typedef uint8_t(__cdecl* FnDbU8)(void**);
typedef int64_t(__cdecl* FnDbI64)(void**);
typedef void(__cdecl* FnDbIdx)(void**, int32_t);
typedef const char* (__cdecl* FnDbName)(void**, uint64_t);
typedef uint8_t(__cdecl* FnU8)(void**);
typedef int32_t(__cdecl* FnI32)(void**);
typedef uint64_t(__cdecl* FnU64)(void**);
typedef const char* (__cdecl* FnStrG)(void**);
int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  std::string mode = argv[2];
  FsHandle fs{};
  if (mode == "win") { fn<FnCreate>("DLL_FileSystem_CreateFileSystemWindows")(&fs); printf("addfolder=%d\n", fn<FnStr>("DLL_FileSystem_AddFolder")(&fs, argv[3])); }
  else { fn<FnCreate>("DLL_FileSystem_CreateFileSystemArchive")(&fs); printf("addarchive=%d\n", fn<FnStr>("DLL_FileSystem_AddArchive")(&fs, argv[3])); }
  void* wide = fn<FnWide>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&fs);
  void* cats = fn<FnEnumNew>("DLL_EnumData_CreateEnumData")();
  void* cols = fn<FnEnumNew>("DLL_EnumData_CreateEnumData")();
  auto add = fn<FnEnumAdd>("DLL_EnumData_AddMember");
  const char* cn[] = {"wear", "skirt", "mizugi", "shoes", "hairf", "body", "headset", "bra", "panz", "kousoku_upper", "acchat", "set_maidwear"};
  for (int i = 0; i < 12; ++i) add(cats, 10 + i, cn[i]);
  for (int i = 0; i < 20; ++i) { char b[16]; sprintf_s(b, "c%d", i); add(cols, 100 + i, b); }
  add(cols, 77, "\xE9\xBB\x92"); /* a UTF-8 name */
  void* db = nullptr;
  fn<FnDbNew>("DLL_MenuDataBase_New")(&db, cats, cols);
  fn<FnDbStart>("DLL_MenuDataBase_StartAnalysis")(&db, wide);
  auto done = fn<FnDbU8>("DLL_MenuDataBase_JobFinished");
  for (int i = 0; i < 6000 && !done(&db); ++i) Sleep(10);
  printf("finished=%d size=%lld\n", done(&db), (long long)fn<FnDbI64>("DLL_MenuDataBase_Size")(&db));
  auto size = fn<FnDbI64>("DLL_MenuDataBase_Size")(&db);
  auto setidx = fn<FnDbIdx>("DLL_MenuDataBase_SetIndex");
  auto fname = fn<FnDbName>("DLL_MenuDataBase_GetFileNameToNativeHash");
  auto pname = fn<FnDbName>("DLL_MenuDataBase_GetParentFileNameToNativeHash");
  auto hdr = fn<FnU8>("DLL_MenuParse_GetHeaderCheck"); auto ver = fn<FnI32>("DLL_MenuParse_GetVersion");
  auto hash = fn<FnU64>("DLL_MenuParse_GetNativeHash");
  FnStrG sg[] = {fn<FnStrG>("DLL_MenuParse_GetSrcFileName"), fn<FnStrG>("DLL_MenuParse_GetItemName"), fn<FnStrG>("DLL_MenuParse_GetCategoryName"),
    fn<FnStrG>("DLL_MenuParse_GetInfoText"), fn<FnStrG>("DLL_MenuParse_GetMenuName"), fn<FnStrG>("DLL_MenuParse_GetItemInfoText"),
    fn<FnStrG>("DLL_MenuParse_GetCategoryMpnText"), fn<FnStrG>("DLL_MenuParse_GetMenuNameInColorSet"), fn<FnStrG>("DLL_MenuParse_GetIconS"),
    fn<FnStrG>("DLL_MenuParse_GetSaveItem"), fn<FnStrG>("DLL_MenuParse_GetPriorityText")};
  auto cmpn = fn<FnI32>("DLL_MenuParse_GetCategoryMpn"); auto csmpn = fn<FnI32>("DLL_MenuParse_GetColorSetMpn"); auto mcid = fn<FnI32>("DLL_MenuParse_GetMultiColorId");
  auto bodel = fn<FnU8>("DLL_MenuParse_GetBoDelOnly"); auto isman = fn<FnU8>("DLL_MenuParse_GetIsMan"); auto iscol = fn<FnU8>("DLL_MenuParse_GetIsCollabo");
  for (int64_t i = 0; i < size; ++i) {
    setidx(&db, (int32_t)i);
    uint64_t h = hash(&db);
    const char* f = fname(&db, h); const char* p = pname(&db, h);
    printf("[%lld] hdr=%d ver=%d hash=%llx cat=%d cs=%d mc=%d del=%d man=%d col=%d file=%s parent=%s", (long long)i, hdr(&db), ver(&db), (unsigned long long)h,
      cmpn(&db), csmpn(&db), mcid(&db), bodel(&db), isman(&db), iscol(&db), f ? f : "(null)", p ? p : "(null)");
    for (auto g : sg) printf(" |%s", g(&db));
    printf("\n");
  }
  printf("unknown hash name=%s\n", fname(&db, 12345) ? "found" : "(null)");
  fn<FnDbDel>("DLL_MenuDataBase_Delete")(&db);
  printf("deleted=%d\n", db == nullptr);
  return 0;
}
