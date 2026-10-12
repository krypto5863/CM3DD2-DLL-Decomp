// KAG corners through the ORIGINAL or the reconstructed DLL: tag enable flags, RemoveTag/AddTag, unique ids,
// and a save / restore round trip of a string scenario into a second object.
// Usage: kag_misc_probe.exe <dll> <scratch folder>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
static void* g_kag = nullptr;
static void* g_second = nullptr;
static void** g_current = &g_kag;

static std::string getString(const char* name, void** handle) {
  char buffer[1024] = {};
  fn<void(__cdecl*)(void**, char*, int)>(name)(handle, buffer, sizeof(buffer));
  return buffer;
}
static std::string visible(const std::string& text) {
  std::string out;
  for (char c : text) { if (c == '\n') out += "\\n"; else out += c; }
  return out;
}
static void state(const char* title, void** handle) {
  std::printf("%s: exec=%d line=%d label='%s' storage='%s' text='%s' depth=%d rdepth=%d\n", title,
    fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(handle), fn<int(__cdecl*)(void**)>("DLL_KAG_GetCurrentLine")(handle),
    getString("DLL_KAG_GetCurrentLabel", handle).c_str(), getString("DLL_KAG_GetStoragename", handle).c_str(),
    visible(getString("DLL_KAG_GetText", handle)).c_str(), fn<int(__cdecl*)(void**)>("DLL_KAG_GetCallStackDepth")(handle),
    fn<int(__cdecl*)(void**)>("DLL_KAG_GetRCallStackDepth")(handle));
}
static void run(const char* title, void** handle, int maxExec = 6) {
  std::printf("== %s\n", title);
  for (int i = 0; i < maxExec; ++i) {
    const int result = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(handle);
    std::printf("exec #%d -> %d\n", i, result);
    state("after", handle);
    if (!fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(handle)) break;
  }
}
static void loadString(void** handle, const char* text) {
  fn<void(__cdecl*)(void**)>("DLL_KAG_TextClear")(handle);
  fn<void(__cdecl*)(void**)>("DLL_KAG_ClearCallStack")(handle);
  fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenarioString")(handle, text);
}
static std::vector<unsigned char> serialize(void** handle) {
  void* data = fn<void*(__cdecl*)(void**)>("DLL_KAG_CreateSerializeData")(handle);
  const int size = fn<int(__cdecl*)(void*)>("DLL_KAG_GetSerializeDataSize")(data);
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  fn<void(__cdecl*)(void*, void*)>("DLL_KAG_WriteSerializeData")(bytes.data(), data);
  fn<void(__cdecl*)(void*)>("DLL_KAG_ReleaseSerializeData")(data);
  return bytes;
}

static int g_calls = 0;
static bool __cdecl markTag(void* support) {
  std::printf("  [tag mark] class=%llu key=%016llx\n", (unsigned long long)fn<uint64_t(__cdecl*)(void*)>("DLL_KAG_TAG_GetClassUniqueid")(support),
    (unsigned long long)fn<uint64_t(__cdecl*)(void*)>("DLL_KAG_TAG_GetTagKey")(support));
  ++g_calls;
  return false;
}
static bool __cdecl markTag2(void* support) {
  std::printf("  [tag mark2] replaced handler key=%016llx\n", (unsigned long long)fn<uint64_t(__cdecl*)(void*)>("DLL_KAG_TAG_GetTagKey")(support));
  return true;
}

int main(int, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  const std::string folder = argv[2];
  CreateDirectoryA(folder.c_str(), nullptr);
  { std::ofstream f(folder + "\\b.ks", std::ios::binary); f << "*sub\nB1[return]\nB2\n"; }
  { std::ofstream f(folder + "\\s.ks", std::ios::binary); f << "*a\nA1[s]\nA2\n*b\nB1[s]\nB2[macro name=\"mm\"]M[endmacro]\n"; }
  void* tjs = nullptr;
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &tjs);
  struct { void* object; int kind; int reserved; } handle{};
  fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(&handle);
  fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(&handle, folder.c_str());
  void* fs = fn<void*(__cdecl*)(void*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&handle);
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &g_kag);
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &g_second);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&g_kag, fs);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&g_second, fs);
  std::printf("ids: %llu %llu script-ptr-id=%llu\n", (unsigned long long)fn<uint64_t(__cdecl*)(void**)>("DLL_KAG_GetUniqueID")(&g_kag),
    (unsigned long long)fn<uint64_t(__cdecl*)(void**)>("DLL_KAG_GetUniqueID")(&g_second),
    (unsigned long long)fn<uint64_t(__cdecl*)(const void*)>("DLL_KAG_GetUniqueIDFromBasicKagScriptPtr")(g_second));
  fn<void(__cdecl*)(void**, const char*, bool(__cdecl*)(void*))>("DLL_KAG_AddTag")(&g_kag, "mark", markTag);
  fn<void(__cdecl*)(void**, const char*, bool(__cdecl*)(void*))>("DLL_KAG_AddTag")(&g_second, "mark", markTag);

  loadString(&g_kag, "A[mark]B\n");
  run("custom tag with unique ids", &g_kag);

  fn<void(__cdecl*)(void**, const char*, bool(__cdecl*)(void*))>("DLL_KAG_AddTag")(&g_kag, "mark", markTag2);
  loadString(&g_kag, "A[mark]B\n");
  run("AddTag replaces the handler", &g_kag);

  std::printf("remove existing=%d remove missing=%d\n", fn<uint8_t(__cdecl*)(void**, const char*)>("DLL_KAG_RemoveTag")(&g_kag, "mark"),
    fn<uint8_t(__cdecl*)(void**, const char*)>("DLL_KAG_RemoveTag")(&g_kag, "mark"));
  loadString(&g_kag, "A[mark]B\n");
  run("after RemoveTag", &g_kag);

  std::printf("remove builtin 'ch'=%d\n", fn<uint8_t(__cdecl*)(void**, const char*)>("DLL_KAG_RemoveTag")(&g_kag, "ch"));
  loadString(&g_kag, "A[ch text=\"X\"]B\n");
  run("after removing ch", &g_kag);

  fn<void(__cdecl*)(void**, uint8_t)>("DLL_KAG_SetTagCallEnabled")(&g_second, 0);
  loadString(&g_second, "A[call file=\"b.ks\" label=\"*sub\"]C\n");
  run("call disabled", &g_second);
  fn<void(__cdecl*)(void**, uint8_t)>("DLL_KAG_SetTagCallEnabled")(&g_second, 1);
  fn<void(__cdecl*)(void**, uint8_t)>("DLL_KAG_SetTagReturnEnabled")(&g_second, 0);
  loadString(&g_second, "A[call file=\"b.ks\" label=\"*sub\"]C\n");
  run("return disabled", &g_second);
  fn<void(__cdecl*)(void**, uint8_t)>("DLL_KAG_SetTagReturnEnabled")(&g_second, 1);

  // save / restore of a file scenario into a second object
  void* third = nullptr;
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &third);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&third, fs);
  fn<void(__cdecl*)(void**)>("DLL_KAG_TextClear")(&g_kag);
  fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenario")(&g_kag, "s.ks");
  fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&g_kag);
  state("saved source", &g_kag);
  fn<void(__cdecl*)(void**, const char*, const char*)>("DLL_KAG_AddRecordLabel")(&g_kag, "s.ks", "*zz");
  const auto saved = serialize(&g_kag);
  std::printf("saved size=%zu\n", saved.size());
  const auto consumed = fn<int64_t(__cdecl*)(void**, const void*)>("DLL_KAG_Deserialize")(&third, saved.data());
  std::printf("restore consumed=%lld\n", (long long)consumed);
  state("restored", &third);
  fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_GoToLabel")(&third, "*b");
  run("restored then goto *b", &third);
  std::printf("restored record zz=%d\n", fn<uint8_t(__cdecl*)(void**, const char*, const char*)>("DLL_KAG_GetRecordLabel")(&third, "s.ks", "*zz"));
  fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&third);
  fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&g_second);
  fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&g_kag);
  fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&tjs);
  std::printf("done\n");
  return 0;
}
