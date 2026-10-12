// KAG execution through the ORIGINAL or the reconstructed DLL with in-memory scenarios.
// Usage: kag_exec_probe.exe <dll>
// Loads scenario strings, runs Exec until it stops, and prints text, position, tag callbacks and call-stack state.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }

static void* g_kag = nullptr;

static std::string getString(const char* name, void** handle) {
  char buffer[1024] = {};
  fn<void(__cdecl*)(void**, char*, int)>(name)(handle, buffer, sizeof(buffer));
  return buffer;
}

static std::string property(void* support, const char* key) {
  void* variant = nullptr;
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&variant);
  fn<void(__cdecl*)(void*, const char*, void**)>("DLL_KAG_TAG_GetTagProperty")(support, key, &variant);
  char buffer[512] = {};
  fn<void(__cdecl*)(void**, char*, int)>("DLL_TJSVariant_AsString")(&variant, buffer, sizeof(buffer));
  const int type = fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&variant);
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&variant);
  char out[640];
  std::snprintf(out, sizeof(out), "%s(t%d)", buffer, type);
  return out;
}

static bool __cdecl onTag(void* support) {
  std::printf("  [tag] key=%016llx valid(text)=%d valid(zzz)=%d text=%s name=%s\n",
    (unsigned long long)fn<uint64_t(__cdecl*)(void*)>("DLL_KAG_TAG_GetTagKey")(support),
    fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_KAG_TAG_IsValid")(support, "text"),
    fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_KAG_TAG_IsValid")(support, "zzz"),
    property(support, "text").c_str(), property(support, "name").c_str());
  void* list = fn<void*(__cdecl*)(void*)>("DLL_KAG_TAG_CreateTagList")(support);
  (void)list;
  const int count = fn<int(__cdecl*)(void*)>("DLL_KAG_TAG_GetListCount")(support);
  for (int i = 0; i < count; ++i) {
    char name[128] = {}, elm[256] = {};
    fn<void(__cdecl*)(void*, int, char*, int)>("DLL_KAG_TAG_GetListNameAt")(support, i, name, sizeof(name));
    fn<void(__cdecl*)(void*, int, char*, int)>("DLL_KAG_TAG_GetListElmAt")(support, i, elm, sizeof(elm));
    std::printf("    attr %d: %s = %s\n", i, name, elm);
  }
  return std::strcmp(property(support, "stop").c_str(), "1(t4)") == 0;
}

static void state(const char* title) {
  std::printf("%s: exec=%d line=%d label='%s' storage='%s' text='", title,
    fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag), fn<int(__cdecl*)(void**)>("DLL_KAG_GetCurrentLine")(&g_kag),
    getString("DLL_KAG_GetCurrentLabel", &g_kag).c_str(), getString("DLL_KAG_GetStoragename", &g_kag).c_str());
  const std::string text = getString("DLL_KAG_GetText", &g_kag);
  for (char c : text) { if (c == '\n') std::printf("\\n"); else std::putchar(c); }
  std::printf("' depth=%d rdepth=%d\n", fn<int(__cdecl*)(void**)>("DLL_KAG_GetCallStackDepth")(&g_kag),
    fn<int(__cdecl*)(void**)>("DLL_KAG_GetRCallStackDepth")(&g_kag));
}

static void runAll(const char* title, const char* scenario) {
  std::printf("== %s\n", title);
  fn<void(__cdecl*)(void**)>("DLL_KAG_TextClear")(&g_kag);
  fn<void(__cdecl*)(void**)>("DLL_KAG_ClearCallStack")(&g_kag);
  fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenarioString")(&g_kag, scenario);
  state("loaded");
  for (int i = 0; i < 8; ++i) {
    const int result = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&g_kag);
    std::printf("exec #%d -> %d\n", i, result);
    state("after");
    if (!fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag)) break;
  }
}

int main(int, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  void* tjs = nullptr;
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &tjs);
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &g_kag);
  fn<void(__cdecl*)(void**, const char*, bool(__cdecl*)(void*))>("DLL_KAG_AddTag")(&g_kag, "mytag", onTag);
  runAll("plain text and [r]", "*start\nHello[r]\nWorld\n");
  runAll("custom tag stops", "A[mytag text=\"x\" stop=1 n=5]B\nC\n");
  runAll("eval and macro", "[eval exp=\"f.v=3\"]\n[macro name=\"m1\"]M[endmacro]\n[m1]X\n");
  runAll("label jump", "*one\nL1\n[mytag stop=1]\n*two\nL2\n");
  runAll("unknown tag", "[nosuchtag a=1]\nafter\n");
  fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&g_kag);
  fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&tjs);
  std::printf("done\n");
  return 0;
}
