// Console messages printed by KAG for scenario errors, through the ORIGINAL or the reconstructed DLL.
// Usage: kag_console_probe.exe <dll> <scratch folder>   (the folder is created and filled with scenario files)
// Each case is a one-file scenario run to its end; every message that reaches the TJS console callback is printed with the
// case name, so the texts and their order can be compared between the two DLLs.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
static std::string g_current;

static void __cdecl onConsole(const char* message, uint8_t is_exception) {
  std::string text = message;
  for (auto& c : text) if (c == '\n') c = '|';
  std::printf("[%s] exc=%d %s\n", g_current.c_str(), is_exception, text.c_str());
}
static uint8_t __cdecl onTag(void*) { return 0; }

int main(int, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  const std::string folder = argv[2];
  CreateDirectoryA(folder.c_str(), nullptr);
  struct Case { const char* name; const char* text; };
  const std::vector<Case> cases = {
    {"unknown_tag", "@nosuchtag a=1\nX\n"},
    {"return_empty", "@return\nX\n"},
    {"syntax_bracket", "[abc\nX\n"},
    {"unknown_macro", "[nomacro]\nX\n"},
    {"endmacro_alone", "@endmacro\nX\n"},
    {"call_missing_file", "@call file=\"nofile\" label=\"*x\"\nX\n"},
    {"jump_missing_file", "@jump file=\"nofile.ks\"\nX\n"},
    {"jump_missing_label", "@jump label=\"*nolabel\"\nX\n"},
    {"call_missing_label", "@call label=\"*nolabel\"\nX\n"},
    {"if_noend", "@if exp=\"1\"\nX\n"},
    {"endif_alone", "@endif\nX\n"},
    {"else_alone", "@else\nX\n"},
    {"iscript_noend", "@iscript\nvar a = 1;\n"},
    {"iscript_error", "@iscript\nvar a = ;\n@endscript\nX\n"},
    {"if_badexp", "@if exp=\"1 +\"\nX\n@endif\n"},
    {"eval_bad", "@eval exp=\"1 +\"\nX\n"},
    {"eval_ok", "@eval exp=\"f.a = 3\"\nX\n"},
    {"if_true", "@if exp=\"1\"\nA\n@else\nB\n@endif\nX\n"},
    {"if_false", "@if exp=\"0\"\nA\n@elsif exp=\"1\"\nB\n@endif\nX\n"},
    {"iscript_ok", "@iscript\nf.b = 7;\n@endscript\n@eval exp=\"f.c = f.b + 1\"\nX\n"},
    {"target_file", "*top\nT1\n*second\nT2\n"},
    {"jump_file_ok", "@jump file=\"target_file.ks\" label=\"*second\"\nX\n"},
    {"jump_same_label", "@jump label=\"*L\"\nX\n*L\nY\n"},
    {"call_ret_ok", "@call file=\"target_file.ks\"\nX\n"},
    {"call_label_ret", "@call label=\"*sub\"\nX\n@s\n*sub\nS\n@return\n"},
    {"iscript_plain", "@iscript\nvar a = 1;\n@endscript\nX\n"},
    {"iscript_throw", "@iscript\nthrow new Exception(\"boom\");\n@endscript\nX\n"},
    {"labels_two", "*a|first\nA\n*b\nB\n"},
    {"if_runtime_member", "@if exp=\"nosuchvar.x\"\nA\n@endif\nX\n"},
    {"if_runtime_call", "@if exp=\"nosuchfunc()\"\nA\n@endif\nX\n"},
    {"if_runtime_throw", "@if exp=\"(function(){ throw new Exception('boom'); })()\"\nA\n@endif\nX\n"},
    {"elsif_runtime", "@if exp=\"0\"\nA\n@elsif exp=\"nosuchvar.y\"\nB\n@endif\nX\n"},
    {"entity_runtime", "@tag a=&nosuchvar.z;\nX\n"},
    {"entity_syntax", "@tag a=&1+;\nX\n"},
    {"ignore_runtime", "@ignore exp=\"nosuchvar.w\"\nA\n@endignore\nX\n"},
    {"load_missing_direct", "<<MISSING>>"},
    {"rcall_missing", "@r_call file=\"nofilexx\"\nX\n"},
    {"rreturn_empty_stack", "@r_return\nX\n"},
    {"rcall_existing", "@r_call file=\"target_file\"\nX\n@r_return\nY\n"},
    {"macro_ok", "@macro name=\"mm\"\nM%a|\n@endmacro\n@mm a=5\nX\n"},
  };
  for (const auto& c : cases) {
    if (std::strcmp(c.text, "<<MISSING>>") == 0) continue;
    const std::string path = folder + "\\" + c.name + ".ks";
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fputs(c.text, f);
    std::fclose(f);
  }
  void* tjs = nullptr;
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &tjs);
  fn<void(__cdecl*)(void**, void*)>("DLL_TJS_CreateTjsConsoleClass")(&tjs, (void*)onConsole);
  struct { void* object; int kind; int reserved; } handle{};
  fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(&handle);
  fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(&handle, folder.c_str());
  void* fs = fn<void*(__cdecl*)(void*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&handle);
  for (const auto& c : cases) {
    void* kag = nullptr;
    fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &kag);
    fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&kag, fs);
    g_current = c.name;
    fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenario")(&kag, (std::string(c.name) + ".ks").c_str());
    int steps = 0;
    for (; steps < 6; ++steps) {
      const int r = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&kag);
      if (!fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&kag) || r == 1) break;
    }
    std::printf("[%s] done steps=%d running=%d\n", c.name, steps, fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&kag));
    fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&kag);
  }
  {
    // A scenario given as text, with a jump to a label inside it and a jump to a missing label.
    void* kag = nullptr;
    fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &kag);
    fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&kag, fs);
    g_current = "string_scenario";
    fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenarioString")(&kag, "A\n@jump label=\"*x\"\n*x\nB\n@jump label=\"*y\"\n");
    for (int steps = 0; steps < 6; ++steps) {
      const int r = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&kag);
      if (!fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&kag) || r == 1) break;
    }
    std::printf("[string_scenario] done\n");
    fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&kag);
  }
  return 0;
}
