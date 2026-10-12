// AnmParse on real .anm files through the ORIGINAL or the reconstructed DLL.
// Usage: anm_real_probe.exe <dll> <folder with .anm files> [max files]
// For each file and each flag combination it prints ok, curve count and, per curve, path / com id / frame count and an FNV hash
// of every key frame (raw float bits). Files are read-only inputs (extracted copies of game data; none are kept in the repo).
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
struct KeyFrame { float time, value, in_tangent, out_tangent; };

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  const std::string folder = argv[2];
  const int limit = argc > 3 ? std::atoi(argv[3]) : 1 << 30;
  auto create = fn<void(__cdecl*)(void**)>("DLL_AnmParse_New");
  auto del = fn<void(__cdecl*)(void**)>("DLL_AnmParse_Delete");
  auto open = fn<uint8_t(__cdecl*)(void**, const uint8_t*, int32_t, uint8_t, uint8_t, uint8_t)>("DLL_AnmParse_OpenByte");
  auto list_of = fn<void*(__cdecl*)(void**)>("DLL_AnmParse_GetAnimationCurveDataList");
  auto list_size = fn<int64_t(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveDataList_Size");
  auto list_at = fn<void*(__cdecl*)(void*, int32_t)>("DLL_AnmParse_AnimationCurveDataList_At");
  auto get_path = fn<const char*(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveData_GetPath");
  auto com_id = fn<uint32_t(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveData_ComID");
  auto frames_of = fn<void*(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveData_KeyFrameList");
  auto frames_size = fn<int64_t(__cdecl*)(void*)>("DLL_AnmParse_KeyFrameList_Size");
  auto frame_at = fn<void(__cdecl*)(void*, int32_t, KeyFrame*)>("DLL_AnmParse_KeyFrameList_At");

  WIN32_FIND_DATAA found;
  HANDLE find = FindFirstFileA((folder + "\\*.anm").c_str(), &found);
  int files = 0;
  long long curves = 0, frames = 0;
  if (find != INVALID_HANDLE_VALUE) {
    do {
      if (files >= limit) break;
      std::ifstream input(folder + "\\" + found.cFileName, std::ios::binary);
      std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
      ++files;
      for (int flags = 0; flags < 8; ++flags) {
        void* parser = nullptr;
        create(&parser);
        const uint8_t ok = open(&parser, data.data(), static_cast<int32_t>(data.size()), flags & 1, (flags >> 1) & 1, (flags >> 2) & 1);
        std::printf("%s f%d ok=%d", found.cFileName, flags, ok);
        if (ok) {
          void* list = list_of(&parser);
          const int64_t n = list_size(list);
          std::printf(" curves=%lld\n", static_cast<long long>(n));
          for (int32_t i = 0; i < n; ++i) {
            void* curve = list_at(list, i);
            void* kfs = frames_of(curve);
            const int64_t count = frames_size(kfs);
            uint64_t hash = 0xcbf29ce484222325ull;
            for (int32_t f = 0; f < count; ++f) {
              KeyFrame kf{};
              frame_at(kfs, f, &kf);
              const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&kf);
              for (size_t b = 0; b < sizeof(kf); ++b) hash = (hash ^ bytes[b]) * 0x100000001b3ull;
            }
            std::printf("  [%d] '%s' id=%u frames=%lld hash=%016llx\n", i, get_path(curve), com_id(curve), static_cast<long long>(count), static_cast<unsigned long long>(hash));
            ++curves;
            frames += count;
          }
        } else {
          std::printf("\n");
        }
        del(&parser);
      }
    } while (FindNextFileA(find, &found));
    FindClose(find);
  }
  std::printf("files=%d curves=%lld frames=%lld\n", files, curves, frames);
  return 0;
}
