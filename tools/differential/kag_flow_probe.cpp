// KAG flow (files, call/return/jump, r_call/r_return, labels, callbacks) through the ORIGINAL or the reconstructed DLL.
// Usage: kag_flow_probe.exe <dll> <scratch folder>   (the folder is created and filled with scenario files)
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

static std::string getString(const char* name) {
  char buffer[1024] = {};
  fn<void(__cdecl*)(void**, char*, int)>(name)(&g_kag, buffer, sizeof(buffer));
  return buffer;
}

static std::string visible(std::string text) {
  std::string out;
  for (char c : text) { if (c == '\n') out += "\\n"; else out += c; }
  return out;
}

static void __cdecl onReplace(const void* script, const char* text, char* out, int capacity) {
  std::printf("  [cb replace] '%s' script=%d cap=%d\n", text, script == g_kag, capacity);
  std::string value = text;
  if (value == "swap.ks") value = "b.ks";   // exercise the replacement path
  std::snprintf(out, capacity, "%s", value.c_str());
}
static void __cdecl onLoad(const void* script, const char* a, const char* b) { std::printf("  [cb load] '%s' '%s' script=%d\n", a, b, script == g_kag); }
static void __cdecl onCall(const void*, const char* a, const char* b) { std::printf("  [cb call] '%s' '%s'\n", a, b); }
static void __cdecl onRCall(const void*, const char* a, const char* b) { std::printf("  [cb rcall] '%s' '%s'\n", a, b); }
static void __cdecl onReturn(const void*, const char* a, int n) { std::printf("  [cb return] '%s' %d\n", a, n); }
static void __cdecl onRReturn(const void*, const char* a, const char* b) { std::printf("  [cb rreturn] '%s' '%s'\n", a, b); }
static void __cdecl onLabel(void* script, uint8_t seen) { std::printf("  [cb label] seen=%d script=%d\n", seen, script == g_kag); }

static void state(const char* title) {
  std::printf("%s: exec=%d line=%d label='%s' storage='%s' text='%s' depth=%d rdepth=%d\n", title,
    fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag), fn<int(__cdecl*)(void**)>("DLL_KAG_GetCurrentLine")(&g_kag),
    getString("DLL_KAG_GetCurrentLabel").c_str(), getString("DLL_KAG_GetStoragename").c_str(), visible(getString("DLL_KAG_GetText")).c_str(),
    fn<int(__cdecl*)(void**)>("DLL_KAG_GetCallStackDepth")(&g_kag), fn<int(__cdecl*)(void**)>("DLL_KAG_GetRCallStackDepth")(&g_kag));
}

static void runFile(const char* title, const char* storage, const char* label = nullptr) {
  std::printf("== %s\n", title);
  fn<void(__cdecl*)(void**)>("DLL_KAG_TextClear")(&g_kag);
  fn<void(__cdecl*)(void**)>("DLL_KAG_ClearCallStack")(&g_kag);
  fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenario")(&g_kag, storage);
  if (label) fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_GoToLabel")(&g_kag, label);
  state("loaded");
  for (int i = 0; i < 6; ++i) {
    const int result = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&g_kag);
    std::printf("exec #%d -> %d\n", i, result);
    state("after");
    if (!fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag) || result == 1) break;
  }
}

static void write(const std::string& folder, const char* name, const char* text) {
  std::ofstream file(folder + "\\" + name, std::ios::binary);
  file << text;
}

int main(int, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  const std::string folder = argv[2];
  CreateDirectoryA(folder.c_str(), nullptr);
  write(folder, "a.ks", "*start\nA1[call file=\"b.ks\" label=\"*sub\"]\nA2[jump label=\"*end\"]\nA3\n*end\nA4[s]\nA5\n");
  write(folder, "b.ks", "*sub\nB1[return]\nB2\n");
  write(folder, "c.ks", "*c0\nC1[r_call file=\"b.ks\" id=3]\nC2\n");
  write(folder, "d.ks", "*d0\nD1[r_call file=\"b\"]\nD2[r_return]\nD3\n");
  write(folder, "e.ks", "*e0|page one\nE1\n*e1\nE2[mymacro]\n[macro name=\"mymacro\"]M[hf][endmacro]\nE3\n");
  write(folder, "f.ks", "*f0\nF1[call file=\"swap.ks\" label=\"*sub\"]\nF2\n");
  write(folder, "g.ks", "[if exp=\"1\"]G1[else]G2[endif]\n[ch text=\"CH\"][hl][sf]\n[r_return]\nGend\n");
  write(folder, "h.ks", "*h0\nH1[r_call file=\"g.ks\"]\nH2[r_return]\nH3\n");
  write(folder, "i.ks", "*i0\nI1[r_call file=\"b.ks*sub\"]\nI2\n");
  write(folder, "j.ks", "*j0\nJ1[r_call file=\"b.ks\" id=0]\nJ2\n");
  write(folder, "k1.ks", "[call file=\"b.ks\" label=\"*sub\"]X\n");
  write(folder, "k2.ks", "[call storage=\"b.ks\"]X\n");
  write(folder, "k3.ks", "[call target=\"*s\"]X\n[s]\n*s\nS1[return]\nS2\n");
  write(folder, "k4.ks", "[jump file=\"b.ks\"]\nSKIP\n");
  write(folder, "k5.ks", "[jump label=\"*e\"]\nSKIP\n*e\nEND\n");
  write(folder, "k6.ks", "[jump target=\"*e\"]\nSKIP\n*e\nEND\n");
  write(folder, "k7.ks", "[call label=\"*s\"]X\n[s]\n*s\nS1[return]\nS2\n");
  write(folder, "r1.ks", "[r_call file=\"b.txt\"]\n");
  write(folder, "r2.ks", "[r_call file=\"dir/b\"]\n");
  write(folder, "r3.ks", "[r_call file=\"x.y.z\"]\n");
  write(folder, "r6.ks", "[r_call file=\"b\" id=\"7\"]\n[r_return]\n");

  void* tjs = nullptr;
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &tjs);
  struct { void* object; int kind; int reserved; } handle{};
  fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(&handle);
  fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(&handle, folder.c_str());
  void* fs = fn<void*(__cdecl*)(void*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&handle);
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &g_kag);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&g_kag, fs);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetReplaceFunction")(&g_kag, (void*)onReplace);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnScenarioLoadFunction")(&g_kag, (void*)onLoad);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnCallFunction")(&g_kag, (void*)onCall);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnRCallFunction")(&g_kag, (void*)onRCall);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnReturnFunction")(&g_kag, (void*)onReturn);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnRReturnFunction")(&g_kag, (void*)onRReturn);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnLabelCallBackFunction")(&g_kag, (void*)onLabel);

  runFile("call / jump / return", "a.ks");
  runFile("label jump with page", "e.ks", "*e1");
  runFile("replace function on call", "f.ks");
  runFile("r_call with id", "c.ks");
  runFile("r_call + r_return", "d.ks");
  runFile("r_call into g.ks", "h.ks");
  runFile("r_call with label in file", "i.ks");
  runFile("r_call id 0", "j.ks");
  for (const char* name : {"k1.ks", "k2.ks", "k3.ks", "k4.ks", "k5.ks", "k6.ks", "k7.ks", "r1.ks", "r2.ks", "r3.ks", "r6.ks"}) {
    runFile(name, name);
  }
  runFile("record labels again (a.ks)", "a.ks");
  fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&g_kag);
  fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&tjs);
  std::printf("done\n");
  return 0;
}
