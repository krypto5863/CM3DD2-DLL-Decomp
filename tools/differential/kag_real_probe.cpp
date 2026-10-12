// Real KAG scenario files through the ORIGINAL or the reconstructed DLL.
// Usage: kag_real_probe.exe <dll> <folder of .ks files> <tag names file> [-v] [max files]
// Every file in the folder is visible through the Windows file system. A handler that logs the tag and its attribute list is
// registered for each name in the tag names file. For each file the whole log, the accumulated text and the final state are
// hashed (FNV) and printed; -v also prints the full log (use it on a single file to see where two DLLs diverge).
// The scenario files are extracted copies of game data; none are kept in the repository.
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
static void* g_kag = nullptr;
static std::string g_log;
static bool g_verbose = false;

static std::string getString(const char* name) {
  std::vector<char> buffer(1 << 20, 0);
  fn<void(__cdecl*)(void**, char*, int)>(name)(&g_kag, buffer.data(), static_cast<int>(buffer.size()));
  return buffer.data();
}

static uint64_t fnv(const std::string& s, uint64_t h = 0xcbf29ce484222325ull) {
  for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ull;
  return h;
}

// CM3D2_PROBE_TJS=1: real game expressions (@eval, @if exp=, entities) run against f/sf/tf/kag dictionaries, every console
// message goes into the log (hex addresses masked) and the dictionaries are dumped after each file, so the TJS engine
// itself is compared on thousands of real expressions.
static bool g_tjs_mode = false;
static void* g_tjs = nullptr;
static void __cdecl onConsole(const char* message, uint8_t exception) {
  std::string text = message, masked;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '0' && i + 1 < text.size() && text[i + 1] == 'x') {
      size_t j = i + 2;
      while (j < text.size() && std::isxdigit(static_cast<unsigned char>(text[j]))) ++j;
      if (j - (i + 2) >= 8) { masked += "0xADDR"; i = j; continue; }
    }
    masked.push_back(text[i++]);
  }
  g_log += std::string("console ") + (exception ? "E " : "N ") + masked + "\n";
}
static void dumpTjs() {
  void* result = nullptr;
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
  fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_EvalScript")(&g_tjs, "__dump()", result);
  void* view = nullptr;
  fn<void(__cdecl*)(void**, void*)>("DLL_TJSVariant_Reference")(&view, result);
  std::vector<char> buffer(1 << 20, 0);
  fn<void(__cdecl*)(void**, char*, int)>("DLL_TJSVariant_AsString")(&view, buffer.data(), static_cast<int>(buffer.size()));
  g_log += std::string("dump ") + buffer.data() + "\n";
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
}
// CM3D2_PROBE_CALLBACKS=<file of function names>: each name is registered with DLL_TJS_AddMemberFunction like the game's
// ScriptManager.Initialize does; a call logs the function, its arguments (type and text) and returns the integer 1.
static std::map<uint64_t, std::string> g_callback_names;
static void __cdecl onNativeCall(const void* parameters, void* result) {
  const int count = fn<int(__cdecl*)(const void*)>("DLL_TJS_ParamAccess_Size")(parameters);
  const uint64_t key = fn<uint64_t(__cdecl*)(const void*)>("DLL_TJS_ParamAccess_GetNameKey")(parameters);
  const auto found = g_callback_names.find(key);
  std::string line = "native " + (found == g_callback_names.end() ? std::string("?") : found->second) + "(";
  for (int i = 0; i < count; ++i) {
    void* raw = fn<void*(__cdecl*)(const void*, int)>("DLL_TJS_ParamAccess_At")(parameters, i);
    void* view = nullptr;
    fn<void(__cdecl*)(void**, void*)>("DLL_TJSVariant_Reference")(&view, raw);
    const int type = fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&view);
    std::vector<char> text(4096, 0);
    fn<void(__cdecl*)(void**, char*, int)>("DLL_TJSVariant_AsString")(&view, text.data(), static_cast<int>(text.size()));
    std::string shown = text.data();
    for (size_t at = shown.find("0x"); at != std::string::npos; at = shown.find("0x", at + 1)) {
      size_t j = at + 2;
      while (j < shown.size() && std::isxdigit(static_cast<unsigned char>(shown[j]))) ++j;
      if (j - (at + 2) >= 8) { shown.replace(at, j - at, "0xADDR"); }
    }
    line += (i ? ", " : "") + std::to_string(type) + ":" + shown;
  }
  line += ")\n";
  if (std::getenv("CM3D2_PROBE_SHOWKEYS")) std::printf("CALL key %016llx\n", static_cast<unsigned long long>(key));
  g_log += line;
  if (result != nullptr) {
    void* out = nullptr;
    fn<void(__cdecl*)(void**, void*)>("DLL_TJSVariant_Reference")(&out, result);
    fn<void(__cdecl*)(void**, int)>("DLL_TJSVariant_SetInteger")(&out, 1);
  }
}

static std::string g_keys_script;  // CM3D2_PROBE_KEYS=<file>: assignments run after every dictionary reset (keeps void out of tag entities)
static void execTjs(const char* script) {
  void* result = nullptr;  // the original dereferences the result pointer, so always pass a real variant
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
  fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_ExecScript")(&g_tjs, script, result);
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
}

static uint8_t __cdecl onTag(void* support) {
  fn<void(__cdecl*)(void*)>("DLL_KAG_TAG_CreateTagList")(support);
  const int64_t count = fn<int64_t(__cdecl*)(void*)>("DLL_KAG_TAG_GetListCount")(support);
  std::string line = "tag";
  for (int i = 0; i < count; ++i) {
    char name[256] = {}, value[1024] = {};
    fn<void(__cdecl*)(void*, int, char*, int)>("DLL_KAG_TAG_GetListNameAt")(support, i, name, sizeof(name));
    fn<void(__cdecl*)(void*, int, char*, int)>("DLL_KAG_TAG_GetListElmAt")(support, i, value, sizeof(value));
    line += std::string(" ") + name + "=" + value;
  }
  line += "\n";
  g_log += line;
  if (g_verbose) std::fputs(line.c_str(), stdout);
  return 0;
}

static void __cdecl onLoad(const void*, const char* a, const char* b) { g_log += std::string("load ") + a + "|" + b + "\n"; }
static void __cdecl onCall(const void*, const char* a, const char* b) { g_log += std::string("call ") + a + "|" + b + "\n"; }
static void __cdecl onReturn(const void*, const char* a, int n) { g_log += std::string("return ") + a + "|" + std::to_string(n) + "\n"; }
static void __cdecl onLabel(void*, uint8_t seen) { g_log += std::string("label ") + std::to_string(seen) + "\n"; }

// Watchdog: a scenario that loops forever inside one Exec call would stall the whole run. When CM3D2_PROBE_HANG_MS is set the
// probe prints "HANG <file>" and exits with code 3 if one file takes longer than that; CM3D2_PROBE_START=<file> resumes a
// run at that file (files before it in directory order are skipped), so a driver can step past hanging files.
static std::atomic<ULONGLONG> g_progress{0};
static char g_current_file[MAX_PATH] = {};
static DWORD g_hang_ms = 0;

static DWORD WINAPI watchdog(void*) {
  for (;;) {
    Sleep(250);
    if (g_hang_ms != 0 && GetTickCount64() - g_progress.load() > g_hang_ms) {
      std::printf("HANG %s\n", g_current_file);
      std::fflush(stdout);
      ExitProcess(3);
    }
  }
}

// Label names of a scenario file ("*name" at the start of a line, up to the first '|'), as UTF-8 (UTF-8 files stay, anything
// else is read as code page 932, the same detection the engine's text loader uses).
static std::vector<std::string> fileLabels(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
  bool utf8 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.c_str(), static_cast<int>(bytes.size()), nullptr, 0) > 0 || bytes.empty();
  std::string text = bytes;
  if (!utf8) {
    const int wide_size = MultiByteToWideChar(932, 0, bytes.c_str(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(wide_size), L'\0');
    MultiByteToWideChar(932, 0, bytes.c_str(), static_cast<int>(bytes.size()), &wide[0], wide_size);
    const int out = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wide_size, nullptr, 0, nullptr, nullptr);
    text.assign(static_cast<size_t>(out), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wide_size, &text[0], out, nullptr, nullptr);
  }
  std::vector<std::string> labels;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    if (text[at] == '*') {
      std::string name = text.substr(at, end - at);
      const size_t bar = name.find('|');
      if (bar != std::string::npos) name.resize(bar);
      while (!name.empty() && (name.back() == '\r' || name.back() == ' ' || name.back() == '\t')) name.pop_back();
      if (name.size() > 1) labels.push_back(name);
    }
    at = end + 1;
  }
  return labels;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (const char* ms = std::getenv("CM3D2_PROBE_HANG_MS")) {
    g_hang_ms = static_cast<DWORD>(std::atoi(ms));
    g_progress = GetTickCount64();
    CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
  }
  const char* start_at = std::getenv("CM3D2_PROBE_START");
  bool started = start_at == nullptr;
  m = LoadLibraryA(argv[1]);
  const std::string folder = argv[2];
  g_verbose = argc > 4 && std::strcmp(argv[4], "-v") == 0;
  const int limit = argc > 5 ? std::atoi(argv[5]) : (argc > 4 && !g_verbose ? std::atoi(argv[4]) : 1 << 30);
  std::vector<std::string> tags;
  { std::ifstream f(argv[3]); std::string line; while (std::getline(f, line)) if (!line.empty()) tags.push_back(line); }

  // The file system comes first so that, in TJS mode, the TjsScript can get it as its host context (as the game does) and
  // Scripts.execStorage can load the game's own startup scripts (CM3D2_PROBE_STARTUP=<folder with startup.tjs, utils.tjs ...>).
  struct { void* object; int kind; int reserved; } handle{};
  fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(&handle);
  fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(&handle, folder.c_str());
  if (const char* keys = std::getenv("CM3D2_PROBE_KEYS")) {
    std::ifstream in(keys, std::ios::binary);
    g_keys_script.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  const char* startup = std::getenv("CM3D2_PROBE_STARTUP");
  if (startup != nullptr) fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(&handle, startup);
  void* fs = fn<void*(__cdecl*)(void*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&handle);
  g_tjs_mode = std::getenv("CM3D2_PROBE_TJS") != nullptr;
  void* tjs = nullptr;
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(g_tjs_mode ? fs : nullptr, &tjs);
  g_tjs = tjs;
  if (g_tjs_mode) {
    fn<void(__cdecl*)(void**, void*)>("DLL_TJS_CreateTjsConsoleClass")(&g_tjs, (void*)onConsole);
    if (const char* names = std::getenv("CM3D2_PROBE_CALLBACKS")) {
      std::ifstream list(names);
      std::string name;
      while (std::getline(list, name)) {
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
        if (name.empty()) continue;
        const uint64_t key = fn<uint64_t(__cdecl*)(void**, const char*, void*)>("DLL_TJS_AddMemberFunction")(&g_tjs, name.c_str(), (void*)onNativeCall);
        g_callback_names[key] = name;
        if (std::getenv("CM3D2_PROBE_SHOWKEYS")) std::printf("REG %s %016llx\n", name.c_str(), static_cast<unsigned long long>(key));
      }
    }
    if (startup != nullptr) {
      execTjs("Scripts.execStorage('startup.tjs');");
      // deterministic stand-ins for the random helpers the scenario scripts call
      execTjs("global.intrandom = function(a, b) { return a; };");
    }
    execTjs("var f=%[]; var sf=%[]; var kag=%[]; if(typeof global.tf == 'undefined') global.tf=%[]; function __dump(){ return (Dictionary.toStructString incontextof %[\"f\"=>f,\"sf\"=>sf,\"tf\"=>tf,\"kag\"=>kag])(); }");
  }
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &g_kag);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&g_kag, fs);
  for (const auto& tag : tags) {
    if (tag == "return" || tag == "call" || tag == "jump" || tag == "eval" || tag == "s" || tag == "if" || tag == "else" ||
        tag == "elsif" || tag == "endif") continue;  // the parser / script handle these themselves
    fn<void(__cdecl*)(void**, const char*, uint8_t(__cdecl*)(void*))>("DLL_KAG_AddTag")(&g_kag, tag.c_str(), onTag);
  }
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnScenarioLoadFunction")(&g_kag, (void*)onLoad);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnCallFunction")(&g_kag, (void*)onCall);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnReturnFunction")(&g_kag, (void*)onReturn);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetOnLabelCallBackFunction")(&g_kag, (void*)onLabel);

  WIN32_FIND_DATAA found;
  HANDLE find = FindFirstFileA((folder + "\\*.ks").c_str(), &found);
  int files = 0;
  if (find != INVALID_HANDLE_VALUE) {
    do {
      if (!started) {
        if (std::strcmp(found.cFileName, start_at) != 0) continue;
        started = true;
      }
      if (files++ >= limit) break;
      strncpy_s(g_current_file, found.cFileName, _TRUNCATE);
      g_progress = GetTickCount64();
      std::vector<std::string> starts{std::string()};  // empty: from the top of the file
      if (std::getenv("CM3D2_PROBE_LABELS")) {
        // also start at every label of the file, the way the game enters subroutine-style scenarios
        for (const auto& label : fileLabels(folder + "\\" + found.cFileName)) starts.push_back(label);
      }
      for (const auto& label : starts) {
        g_progress = GetTickCount64();
        g_log.clear();
        if (g_tjs_mode) {
          execTjs("f=%[]; sf=%[]; tf=%[]; kag=%[];");
          if (!g_keys_script.empty()) execTjs(g_keys_script.c_str());
        }
        fn<void(__cdecl*)(void**)>("DLL_KAG_TextClear")(&g_kag);
        fn<void(__cdecl*)(void**)>("DLL_KAG_ClearCallStack")(&g_kag);
        fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenario")(&g_kag, found.cFileName);
        if (!label.empty()) fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_GoToLabel")(&g_kag, label.c_str());
        int steps = 0, last = -1;
        for (; steps < 300; ++steps) {
          last = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&g_kag);
          const std::string text = getString("DLL_KAG_GetText");
          g_log += "text " + std::to_string(text.size()) + " " + std::to_string(fnv(text)) + "\n";
          if (!fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag) || last == 1) break;
        }
        if (g_tjs_mode) dumpTjs();
        const std::string text = getString("DLL_KAG_GetText");
        std::printf("%s%s%s steps=%d last=%d exec=%d line=%d label='%s' storage='%s' depth=%d textlen=%zu texthash=%016llx loghash=%016llx logbytes=%zu\n",
          found.cFileName, label.empty() ? "" : "@", label.c_str(), steps, last, fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag),
          fn<int(__cdecl*)(void**)>("DLL_KAG_GetCurrentLine")(&g_kag), getString("DLL_KAG_GetCurrentLabel").c_str(),
          getString("DLL_KAG_GetStoragename").c_str(), fn<int(__cdecl*)(void**)>("DLL_KAG_GetCallStackDepth")(&g_kag), text.size(),
          static_cast<unsigned long long>(fnv(text)), static_cast<unsigned long long>(fnv(g_log)), g_log.size());
        if (g_verbose) std::printf("--- log ---\n%s--- end ---\n", g_log.c_str());
      }
    } while (FindNextFileA(find, &found));
    FindClose(find);
  }
  std::printf("files=%d\n", files > limit ? limit : files);
  return 0;
}
