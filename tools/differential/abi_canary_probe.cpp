// ABI audit: how many bytes of the caller's handle slot does each Create/New export write, and what does the matching Delete
// write? A 64-byte canary-filled buffer is passed; the managed game passes a pointer to an IntPtr-sized slot, so a
// reconstruction that writes more than the original would corrupt the caller's memory. Output: per export, the offsets
// that changed. Usage: abi_canary_probe.exe <dll>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
static unsigned char g_buffer[256];

static void arm() { std::memset(g_buffer, 0xCC, sizeof(g_buffer)); }
static void report(const char* label) {
  std::string changed;
  for (int i = 0; i < 256; ++i) if (g_buffer[i] != 0xCC) { changed += std::to_string(i) + ","; }
  std::printf("%-34s changed: %s\n", label, changed.empty() ? "-" : changed.c_str());
}
template<class Create, class... A> static void createThenDelete(const char* name, const char* create, const char* del, A... args) {
  arm();
  fn<Create>(create)(args..., g_buffer);
  std::string label = std::string(name) + " create";
  report(label.c_str());
  // snapshot the handle value so Delete sees what Create produced
  arm();
}

int main(int, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  struct Case { const char* name; const char* create; const char* del; int kind; };
  const Case cases[] = {
    {"FileSystemWindows", "DLL_FileSystem_CreateFileSystemWindows", "DLL_FileSystem_DeleteFileSystem", 0},
    {"FileSystemArchive", "DLL_FileSystem_CreateFileSystemArchive", "DLL_FileSystem_DeleteFileSystem", 0},
    {"Win32File", "DLL_Win32File_Create", "DLL_Win32File_Delete", 0},
    {"FileMemory", "DLL_FileMemory_CreateileMemory", "DLL_FileMemory_DeleteileMemory", 0},
    {"FileSystemDelegate", "DLL_FileSystemDelegate_Create", "DLL_FileSystemDelegate_Delete", 0},
    {"AnmParse", "DLL_AnmParse_New", "DLL_AnmParse_Delete", 0},
    {"HoleDevice", "DLL_HoleDevice_Create", "DLL_HoleDevice_Delete", 0},
    {"TJSVariant", "DLL_TJSVariant_Create", "DLL_TJSVariant_Delete", 0},
  };
  for (const auto& c : cases) {
    auto create = fn<void(__cdecl*)(void*)>(c.create);
    auto del = fn<void(__cdecl*)(void*)>(c.del);
    if (!create || !del) { std::printf("%-20s missing export\n", c.name); continue; }
    arm();
    create(g_buffer);
    report((std::string(c.name) + " create").c_str());
    unsigned char saved[256];
    std::memcpy(saved, g_buffer, sizeof(saved));
    // Delete gets a fresh canary tail but the same handle bytes
    del(g_buffer);
    std::string changed;
    for (int i = 0; i < 256; ++i) if (g_buffer[i] != saved[i]) changed += std::to_string(i) + ",";
    std::printf("%-34s changed: %s\n", (std::string(c.name) + " delete").c_str(), changed.empty() ? "-" : changed.c_str());
  }
  // TJS script, KAG on top of it, OGG reader, menu database
  {
    arm();
    fn<void(__cdecl*)(void*, void*)>("DLL_TJS_CreateTjsScriptClass")(nullptr, g_buffer);
    report("TjsScript create");
    unsigned char tjs[256]; std::memcpy(tjs, g_buffer, sizeof(tjs));
    void* tjs_handle = nullptr; std::memcpy(&tjs_handle, tjs, 8);
    static unsigned char kag[256];
    std::memset(kag, 0xCC, sizeof(kag));
    fn<void(__cdecl*)(void*, void*)>("DLL_KAG_Create")(g_buffer, kag);
    std::string changed;
    for (int i = 0; i < 256; ++i) if (kag[i] != 0xCC) changed += std::to_string(i) + ",";
    std::printf("%-34s changed: %s\n", "Kag create", changed.empty() ? "-" : changed.c_str());
    unsigned char before[256]; std::memcpy(before, kag, sizeof(before));
    fn<void(__cdecl*)(void*)>("DLL_KAG_Delete")(kag);
    changed.clear();
    for (int i = 0; i < 256; ++i) if (kag[i] != before[i]) changed += std::to_string(i) + ",";
    std::printf("%-34s changed: %s\n", "Kag delete", changed.empty() ? "-" : changed.c_str());
    std::memcpy(before, g_buffer, sizeof(before));
    fn<void(__cdecl*)(void*)>("DLL_TJS_DeleteTjsScriptClass")(g_buffer);
    changed.clear();
    for (int i = 0; i < 256; ++i) if (g_buffer[i] != before[i]) changed += std::to_string(i) + ",";
    std::printf("%-34s changed: %s\n", "TjsScript delete", changed.empty() ? "-" : changed.c_str());
  }
  {
    // OGG reader on a Windows file system handle's wide pointer
    unsigned char fsh[256]; std::memset(fsh, 0xCC, sizeof(fsh));
    fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(fsh);
    void* wide = fn<void*(__cdecl*)(void*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(fsh);
    arm();
    fn<void(__cdecl*)(void*, void*)>("DLL_OGG_CreateSoundReaderOGG")(wide, g_buffer);
    report("OggReader create");
    unsigned char before[256]; std::memcpy(before, g_buffer, sizeof(before));
    fn<void(__cdecl*)(void*)>("DLL_OGG_DeleteSoundReaderOGG")(g_buffer);
    std::string changed;
    for (int i = 0; i < 256; ++i) if (g_buffer[i] != before[i]) changed += std::to_string(i) + ",";
    std::printf("%-34s changed: %s\n", "OggReader delete", changed.empty() ? "-" : changed.c_str());
    void* enumData = fn<void*(__cdecl*)()>("DLL_EnumData_CreateEnumData")();
    arm();
    fn<void(__cdecl*)(void*, void*, void*)>("DLL_MenuDataBase_New")(g_buffer, enumData, wide);
    report("MenuDataBase create");
    // MenuDataBase_Delete is not called: the original deadlocks when no analysis was started (documented hardening difference).
    fn<void(__cdecl*)(void*)>("DLL_EnumData_DeleteEnumData")(enumData);
  }
  {
    // list handles and file handles handed out by a file system with one folder
    unsigned char fsh[256]; std::memset(fsh, 0xCC, sizeof(fsh));
    fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(fsh);
    char dir[MAX_PATH]; GetTempPathA(MAX_PATH, dir);
    std::string root = std::string(dir) + "cm3d2_abi_canary";
    CreateDirectoryA(root.c_str(), nullptr);
    HANDLE h = CreateFileA((root + "/a.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    CloseHandle(h);
    fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(fsh, root.c_str());
    arm();
    fn<void(__cdecl*)(void*, const char*, int, void*)>("DLL_FileSystem_CreateList")(fsh, "", 2, g_buffer);
    report("CreateList");
    fn<void(__cdecl*)(void*)>("DLL_FileSystem_DeleteList")(g_buffer);
    arm();
    fn<void(__cdecl*)(void*, const char*, void*)>("DLL_FileSystem_CreateExtensionList")(fsh, "txt", g_buffer);
    report("CreateExtensionList");
    fn<void(__cdecl*)(void*)>("DLL_FileSystem_DeleteList")(g_buffer);
    arm();
    const uint8_t got = fn<uint8_t(__cdecl*)(void*, const char*, void*)>("DLL_FileSystem_GetFile")(fsh, "a.txt", g_buffer);
    std::printf("GetFile existing returned %d\n", got);
    report("GetFile existing");
    if (got) fn<void(__cdecl*)(void*)>("DLL_File_CloseFile")(g_buffer);
    arm();
    const uint8_t missing = fn<uint8_t(__cdecl*)(void*, const char*, void*)>("DLL_FileSystem_GetFile")(fsh, "nope.txt", g_buffer);
    std::printf("GetFile missing returned %d\n", missing);
    report("GetFile missing");
    arm();
    unsigned char fh[256]; std::memset(fh, 0xCC, sizeof(fh));
    fn<void(__cdecl*)(void*)>("DLL_Win32File_Create")(fh);
    std::memcpy(g_buffer, fh, sizeof(fh));
    const wchar_t* wide = L"nope.txt";
    fn<uint8_t(__cdecl*)(void*, const wchar_t*)>("DLL_Win32File_Open")(g_buffer, wide);
    unsigned char after[256]; std::memcpy(after, g_buffer, sizeof(after));
    std::string changed;
    for (int i = 0; i < 256; ++i) if (after[i] != fh[i]) changed += std::to_string(i) + ",";
    std::printf("%-34s changed: %s\n", "Win32File open (missing)", changed.empty() ? "-" : changed.c_str());
  }
  // unloading the library after use must not crash (the game unloads it at exit)
  std::printf("FreeLibrary=%d\n", FreeLibrary(m) ? 1 : 0);
  return 0;
}
