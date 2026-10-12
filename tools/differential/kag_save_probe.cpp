// KAG save/restore compatibility through the ORIGINAL or the reconstructed DLL.
// Usage: kag_save_probe.exe save   <dll> <folder> <file.ks> <stop after K tags> <out.bin> <tag names file>
//        kag_save_probe.exe resume <dll> <folder> <in.bin> <run N tags> <tag names file>
// save: loads the scenario, runs until the K-th tag (the handler asks Exec to stop there), writes the serialized state and prints
// the tags seen and the final parser state. resume: a fresh object restores the state with Deserialize and runs on for N tags;
// the printed continuation can be compared between a save made by one DLL and a resume in the other.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
static void* g_kag = nullptr;
static int g_limit = 0, g_seen = 0;

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
  std::printf("%s\n", line.c_str());
  ++g_seen;
  return g_seen >= g_limit ? 1 : 0;  // stop Exec once the limit is reached
}

static std::string getString(const char* name) {
  std::vector<char> buffer(1 << 16, 0);
  fn<void(__cdecl*)(void**, char*, int)>(name)(&g_kag, buffer.data(), static_cast<int>(buffer.size()));
  return buffer.data();
}

static void state(const char* title) {
  std::printf("%s: exec=%d line=%d label='%s' storage='%s' depth=%d text=%zu\n", title,
    fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag), fn<int(__cdecl*)(void**)>("DLL_KAG_GetCurrentLine")(&g_kag),
    getString("DLL_KAG_GetCurrentLabel").c_str(), getString("DLL_KAG_GetStoragename").c_str(),
    fn<int(__cdecl*)(void**)>("DLL_KAG_GetCallStackDepth")(&g_kag), getString("DLL_KAG_GetText").size());
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const std::string mode = argv[1];
  m = LoadLibraryA(argv[2]);
  const std::string folder = argv[3];
  const std::string tagsFile = argv[mode == "save" ? 7 : 6];
  void* tjs = nullptr;
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &tjs);
  struct { void* object; int kind; int reserved; } handle{};
  fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(&handle);
  fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(&handle, folder.c_str());
  void* fs = fn<void*(__cdecl*)(void*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&handle);
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &g_kag);
  fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&g_kag, fs);
  std::ifstream list(tagsFile);
  std::string tag;
  while (std::getline(list, tag)) {
    if (tag.empty() || tag == "return" || tag == "call" || tag == "jump" || tag == "eval" || tag == "s" || tag == "if" || tag == "else" ||
        tag == "elsif" || tag == "endif") continue;
    fn<void(__cdecl*)(void**, const char*, uint8_t(__cdecl*)(void*))>("DLL_KAG_AddTag")(&g_kag, tag.c_str(), onTag);
  }
  if (mode == "save") {
    g_limit = std::atoi(argv[5]);
    fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenario")(&g_kag, argv[4]);
    for (int i = 0; i < 400 && fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag); ++i) {
      const int r = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&g_kag);
      if (r == 1) break;
    }
    state("saved");
    void* data = fn<void*(__cdecl*)(void**)>("DLL_KAG_CreateSerializeData")(&g_kag);
    const int size = fn<int(__cdecl*)(void*)>("DLL_KAG_GetSerializeDataSize")(data);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    fn<void(__cdecl*)(void*, void*)>("DLL_KAG_WriteSerializeData")(bytes.data(), data);
    fn<void(__cdecl*)(void*)>("DLL_KAG_ReleaseSerializeData")(data);
    std::ofstream out(argv[6], std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::printf("size=%d\n", size);
  } else {
    std::ifstream in(argv[4], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    g_limit = std::atoi(argv[5]);
    std::printf("deserialize=%lld\n", static_cast<long long>(fn<int64_t(__cdecl*)(void**, const void*)>("DLL_KAG_Deserialize")(&g_kag, bytes.data())));
    state("restored");
    for (int i = 0; i < 400 && fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&g_kag); ++i) {
      const int r = fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&g_kag);
      if (r == 1) break;
    }
    state("continued");
  }
  return 0;
}
