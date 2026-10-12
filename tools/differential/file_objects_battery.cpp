// FileMemory / Win32File differential battery. A fixed-seed random operation sequence is applied through the exports AND through the
// interface vtable (slot 3 read, 4 readAt, 5 valid, 6 data, 7 tell, 8 size, 2 seek) and the results are printed, so the ORIGINAL and
// the reconstruction can be diffed. Only owned temp files are touched.
// Usage: file_objects_battery.exe <dll> <work dir>
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct FileH { void* object; };
static HMODULE m;
template <class T> T fn(const char* n) { T p = reinterpret_cast<T>(GetProcAddress(m, n)); if (!p) { std::printf("missing %s\n", n); std::exit(3); } return p; }

static uint64_t g_state = 0x9e3779b97f4a7c15ull;
static uint64_t rnd() { g_state ^= g_state << 13; g_state ^= g_state >> 7; g_state ^= g_state << 17; return g_state; }
static int64_t edgeOffset() {
  switch (rnd() % 14) {
    case 0: return 0;
    case 1: return 1;
    case 2: return -1;
    case 3: return (int64_t)(rnd() % 40);
    case 4: return -(int64_t)(rnd() % 40);
    case 5: return INT64_MAX;
    case 6: return INT64_MIN;
    case 7: return INT64_MAX - 3;
    case 8: return INT64_MIN + 3;
    case 9: return (int64_t)1 << 32;
    case 10: return -((int64_t)1 << 32);
    default: return (int64_t)(rnd() % 200) - 100;
  }
}
static uint64_t edgeCount() {
  switch (rnd() % 9) {
    case 0: return 0;
    case 1: return 1;
    case 2: return UINT64_MAX;
    case 3: return (uint64_t)INT64_MAX;
    case 4: return (uint64_t)INT64_MAX + 1;
    case 5: return rnd() % 8;
    case 6: return 100000;
    default: return rnd() % 64;
  }
}
static uint64_t fnv(const unsigned char* b, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull; return h; }

typedef uint64_t (__cdecl* ReadFn)(void*, void*, uint64_t);
typedef uint64_t (__cdecl* ReadAtFn)(void*, void*, uint64_t, uint64_t);
typedef unsigned char (__cdecl* ValidFn)(void*);
typedef const void* (__cdecl* DataFn)(void*);
typedef uint64_t (__cdecl* U64Fn)(void*);
typedef unsigned char (__cdecl* SeekFn)(void*, int64_t, unsigned char);

static void run(const char* label, FileH& file, void* iface, int ops) {
  void** vt = *(void***)iface;
  auto setSeek = fn<uint32_t(__cdecl*)(FileH*, int64_t, uint8_t)>("DLL_File_SetSeek");
  auto read = fn<uint64_t(__cdecl*)(FileH*, void*, uint64_t)>("DLL_File_Read");
  auto tell = fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetTell");
  auto size = fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetSize");
  auto valid = fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid");
  std::vector<unsigned char> buffer(70000);
  for (int i = 0; i < ops; ++i) {
    switch (rnd() % 7) {
      case 0: { const int64_t o = edgeOffset(); const uint8_t a = (uint8_t)(rnd() & 1); const uint32_t r = setSeek(&file, o, a); std::printf("%s #%d seek(%lld,%u)=%u tell=%llu\n", label, i, (long long)o, a, r & 0xff, (unsigned long long)tell(&file)); break; }
      case 1: { std::fill(buffer.begin(), buffer.end(), 0xCC); uint64_t c = edgeCount(); if (c > 60000) c = rnd() % 60000; const uint64_t got = read(&file, buffer.data(), c); std::printf("%s #%d read(%llu)=%llu hash=%016llx tell=%llu\n", label, i, (unsigned long long)c, (unsigned long long)got, (unsigned long long)fnv(buffer.data(), (size_t)(got < 60000 ? got : 60000)), (unsigned long long)tell(&file)); break; }
      case 2: { std::fill(buffer.begin(), buffer.end(), 0xCC); uint64_t off = (uint64_t)edgeOffset(); uint64_t c = edgeCount(); if (c > 60000) c = rnd() % 60000; const uint64_t got = ((ReadAtFn)vt[4])(iface, buffer.data(), off, c); std::printf("%s #%d readAt(%llu,%llu)=%llu hash=%016llx tell=%llu\n", label, i, (unsigned long long)off, (unsigned long long)c, (unsigned long long)got, (unsigned long long)fnv(buffer.data(), (size_t)(got < 60000 ? got : 60000)), (unsigned long long)tell(&file)); break; }
      case 3: std::printf("%s #%d size=%llu tell=%llu valid=%u slotvalid=%u\n", label, i, (unsigned long long)size(&file), (unsigned long long)tell(&file), valid(&file) & 0xff, ((ValidFn)vt[5])(iface)); break;
      case 4: { const unsigned char* d = (const unsigned char*)((DataFn)vt[6])(iface); const bool has = size(&file) > 0 && d != nullptr; std::printf("%s #%d data=%s first=%02x\n", label, i, d ? "yes" : "null", has ? d[0] : 0); break; }
      case 5: { const int64_t o = edgeOffset(); const unsigned char a = (unsigned char)(rnd() & 1); const unsigned char r = ((SeekFn)vt[2])(iface, o, a); std::printf("%s #%d slotseek(%lld,%u)=%u slottell=%llu\n", label, i, (long long)o, a, r, (unsigned long long)((U64Fn)vt[7])(iface)); break; }
      default: { const uint64_t c = edgeCount() % 70; std::fill(buffer.begin(), buffer.end(), 0xCC); const uint64_t got = ((ReadFn)vt[3])(iface, buffer.data(), c); std::printf("%s #%d slotread(%llu)=%llu hash=%016llx\n", label, i, (unsigned long long)c, (unsigned long long)got, (unsigned long long)fnv(buffer.data(), (size_t)got)); break; }
    }
  }
}

int main(int, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  if (!m) return 3;
  const std::string work = argv[2];
  CreateDirectoryA(work.c_str(), nullptr);
  unsigned char payload[300];
  for (int i = 0; i < 300; ++i) payload[i] = (unsigned char)((i * 37 + 11) & 0xff);
  // FileMemory with several payload sizes, including 0 and re-SetData.
  for (int sizeCase : {0, 1, 7, 64, 300}) {
    FileH file{};
    fn<void(__cdecl*)(FileH*)>("DLL_FileMemory_CreateileMemory")(&file);
    void* iface = fn<void*(__cdecl*)(FileH*)>("DLL_FileMemory_GetInterfaceFilePointer")(&file);
    std::printf("== FileMemory size %d: valid before SetData=%u size=%llu\n", sizeCase, fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid")(&file) & 0xff,
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetSize")(&file));
    fn<void(__cdecl*)(FileH*, const void*, int32_t)>("DLL_FileMemory_SetData")(&file, payload, sizeCase);
    std::printf("after SetData: valid=%u size=%llu tell=%llu\n", fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid")(&file) & 0xff,
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetSize")(&file), (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetTell")(&file));
    g_state = 0x9e3779b97f4a7c15ull + sizeCase;
    run("fm", file, iface, 160);
    // SetData again with another size: position and size reset.
    fn<void(__cdecl*)(FileH*, const void*, int32_t)>("DLL_FileMemory_SetData")(&file, payload, 5);
    std::printf("after second SetData(5): valid=%u size=%llu tell=%llu\n", fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid")(&file) & 0xff,
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetSize")(&file), (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetTell")(&file));
    run("fm2", file, iface, 40);
    // Close through the slot: state after close (slot 1).
    typedef void (__cdecl* CloseFn)(void*);
    ((CloseFn)(*(void***)iface)[1])(iface);
    std::printf("after slot close: valid=%u size=%llu tell=%llu\n", fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid")(&file) & 0xff,
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetSize")(&file), (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetTell")(&file));
    run("fmclosed", file, iface, 30);
    fn<void(__cdecl*)(FileH*)>("DLL_FileMemory_DeleteileMemory")(&file);
  }
  // Win32File over an owned file.
  const std::string path = work + "\\win32file.bin";
  {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w; WriteFile(h, payload, 300, &w, nullptr); CloseHandle(h);
    h = CreateFileA((work + "\\empty.bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); CloseHandle(h);
  }
  for (const char* name : {"win32file.bin", "empty.bin", "missing.bin"}) {
    FileH file{};
    fn<void(__cdecl*)(FileH*)>("DLL_Win32File_Create")(&file);
    const std::string full = work + "\\" + name;
    wchar_t wpath[512]; MultiByteToWideChar(CP_ACP, 0, full.c_str(), -1, wpath, 512);
    const uint32_t opened = fn<uint32_t(__cdecl*)(FileH*, const wchar_t*)>("DLL_Win32File_Open")(&file, wpath);
    std::printf("== Win32File %s: open=%u valid=%u size=%llu\n", name, opened & 0xff, fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid")(&file) & 0xff,
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetSize")(&file));
    g_state = 0x1234567 + (unsigned)strlen(name);
    run("w32", file, file.object, 120);
    fn<void(__cdecl*)(FileH*)>("DLL_Win32File_Delete")(&file);
    std::printf("after delete: handle=%s\n", file.object ? "set" : "null");
  }
  // Null / empty handle behaviour of the export wrappers.
  {
    FileH none{};
    std::printf("null handle: valid=%u read=%llu tell=%llu size=%llu seek=%u\n", fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid")(&none) & 0xff,
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*, void*, uint64_t)>("DLL_File_Read")(&none, payload, 4), (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetTell")(&none),
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*)>("DLL_File_GetSize")(&none), fn<uint32_t(__cdecl*)(FileH*, int64_t, uint8_t)>("DLL_File_SetSeek")(&none, 0, 1) & 0xff);
    std::printf("null pointer: valid=%u read=%llu\n", fn<uint32_t(__cdecl*)(FileH*)>("DLL_File_IsValid")(nullptr) & 0xff,
      (unsigned long long)fn<uint64_t(__cdecl*)(FileH*, void*, uint64_t)>("DLL_File_Read")(nullptr, payload, 4));
  }
  return 0;
}
