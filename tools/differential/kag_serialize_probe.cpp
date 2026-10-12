// KAG record/serialization state through the ORIGINAL or the reconstructed DLL (no scenario needed).
// Usage: kag_serialize_probe.exe <dll>
// Creates a TjsScript and a KAG object, records labels and finish files, serializes, then deserializes
// hand-built payloads (no parser state text: restoring one needs a scenario file system and crashes the
// original without it) into a second object and serializes again.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }

static void dump(const char* title, const std::vector<unsigned char>& bytes, size_t limit = 96) {
  printf("%s size=%zu\n", title, bytes.size());
  for (size_t i = 0; i < bytes.size() && i < limit; i += 32) {
    printf("  %04zx:", i);
    for (size_t j = i; j < i + 32 && j < bytes.size(); ++j) printf(" %02x", bytes[j]);
    printf("\n");
  }
}

static std::vector<unsigned char> serialize(void** kag) {
  void* data = fn<void*(__cdecl*)(void**)>("DLL_KAG_CreateSerializeData")(kag);
  const int size = fn<int(__cdecl*)(void*)>("DLL_KAG_GetSerializeDataSize")(data);
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  fn<void(__cdecl*)(void*, void*)>("DLL_KAG_WriteSerializeData")(bytes.data(), data);
  fn<void(__cdecl*)(void*)>("DLL_KAG_ReleaseSerializeData")(data);
  return bytes;
}

int main(int, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  void* tjs = nullptr;
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &tjs);
  void* kag = nullptr;
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &kag);
  auto addLabel = fn<void(__cdecl*)(void**, const char*, const char*)>("DLL_KAG_AddRecordLabel");
  auto getLabel = fn<uint8_t(__cdecl*)(void**, const char*, const char*)>("DLL_KAG_GetRecordLabel");
  auto addFinish = fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_AddFinishFile");
  auto getFinish = fn<uint8_t(__cdecl*)(void**, const char*)>("DLL_KAG_GetFinishFile");
  auto rdepth = fn<int32_t(__cdecl*)(void**)>("DLL_KAG_GetRCallStackDepth");
  printf("unique id: %llu\n", (unsigned long long)fn<uint64_t(__cdecl*)(void**)>("DLL_KAG_GetUniqueID")(&kag));
  const auto empty = serialize(&kag);
  dump("empty", empty, 700);
  addLabel(&kag, "a.ks", "*x");
  addLabel(&kag, "a.ks", "*y");
  addLabel(&kag, "a.ks", "*x");
  addLabel(&kag, "b.ks", "*x");
  addFinish(&kag, "c.ks");
  addFinish(&kag, "a.ks");
  printf("get a.ks/*x=%d a.ks/*z=%d b.ks/*x=%d b.ks/*y=%d c.ks/*x=%d\n", getLabel(&kag, "a.ks", "*x"), getLabel(&kag, "a.ks", "*z"),
    getLabel(&kag, "b.ks", "*x"), getLabel(&kag, "b.ks", "*y"), getLabel(&kag, "c.ks", "*x"));
  printf("finish c.ks=%d a.ks=%d d.ks=%d\n", getFinish(&kag, "c.ks"), getFinish(&kag, "a.ks"), getFinish(&kag, "d.ks"));
  printf("tag enabled: call=%d return=%d\n", fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_GetTagCallEnabled")(&kag),
    fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_GetTagReturnEnabled")(&kag));
  fn<void(__cdecl*)(void**, uint8_t)>("DLL_KAG_SetTagCallEnabled")(&kag, 0);
  fn<void(__cdecl*)(void**, uint8_t)>("DLL_KAG_SetTagReturnEnabled")(&kag, 1);
  printf("tag enabled after set: call=%d return=%d\n", fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_GetTagCallEnabled")(&kag),
    fn<uint8_t(__cdecl*)(void**)>("DLL_KAG_GetTagReturnEnabled")(&kag));
  const auto first = serialize(&kag);
  dump("filled", first, 96);

  uint32_t record_bytes = 0;
  std::memcpy(&record_bytes, first.data(), 4);
  std::vector<unsigned char> payload(first.begin(), first.begin() + 4 + record_bytes);
  const auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) payload.push_back(static_cast<unsigned char>(v >> (8 * i))); };
  const auto putText = [&](const wchar_t* text) {
    const uint32_t bytes = static_cast<uint32_t>((wcslen(text) + 1) * 2);
    put32(bytes);
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    payload.insert(payload.end(), p, p + bytes);
  };
  put32(0);                // no state text
  payload.push_back(1);    // flag
  put32(2);                // two record-call entries
  putText(L"x.ks"); putText(L"*lab"); put32(7);
  putText(L"yy.ks"); putText(L""); put32(0xffffffffu);

  void* second = nullptr;
  fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &second);
  printf("depth before=%d\n", rdepth(&second));
  const auto consumed = fn<uint64_t(__cdecl*)(void**, const void*)>("DLL_KAG_Deserialize")(&second, payload.data());
  printf("deserialize consumed=%llu (of %zu)\n", (unsigned long long)consumed, payload.size());
  printf("depth after=%d\n", rdepth(&second));
  printf("second get a.ks/*x=%d b.ks/*x=%d a.ks/*z=%d finish c.ks=%d\n", getLabel(&second, "a.ks", "*x"), getLabel(&second, "b.ks", "*x"),
    getLabel(&second, "a.ks", "*z"), getFinish(&second, "c.ks"));
  const auto again = serialize(&second);
  dump("again head", again, 96);
  {
    uint32_t block = 0, text_bytes = 0;
    std::memcpy(&block, again.data(), 4);
    std::memcpy(&text_bytes, again.data() + 4 + block, 4);
    const wchar_t* wide = reinterpret_cast<const wchar_t*>(again.data() + 8 + block);
    char utf8[2048] = {};
    WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(text_bytes / 2) - 1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
    printf("again state text: %s\n", utf8);
  }
  printf("again size=%zu tail:", again.size());
  for (size_t i = again.size() - 53; i < again.size(); ++i) printf(" %02x", again[i]);
  printf("\n");

  std::vector<unsigned char> junk(64, 0);
  junk[0] = 8;
  for (int i = 4; i < 12; ++i) junk[i] = 0x11;  // size 8, bad magic, no text, no flag, no records
  const auto junk_consumed = fn<uint64_t(__cdecl*)(void**, const void*)>("DLL_KAG_Deserialize")(&second, junk.data());
  printf("junk consumed=%llu get a.ks/*x=%d depth=%d\n", (unsigned long long)junk_consumed, getLabel(&second, "a.ks", "*x"), rdepth(&second));
  fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&second);
  fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&kag);
  fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&tjs);
  printf("done\n");
  return 0;
}
