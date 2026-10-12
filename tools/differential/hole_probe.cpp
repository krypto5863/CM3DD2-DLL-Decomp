// HoleDevice state logic through the ORIGINAL or the reconstructed DLL (no hardware needed).
// Usage: hole_probe.exe <dll>
#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
int main(int, char** argv) {
  m = LoadLibraryA(argv[1]);
  void* d = nullptr;
  fn<void(__cdecl*)(void**)>("DLL_HoleDevice_Create")(&d);
  auto getf = [&](const char* n) { return fn<float(__cdecl*)(void**)>(n)(&d); };
  auto geti = [&](const char* n) { return fn<int32_t(__cdecl*)(void**)>(n)(&d); };
  auto getb = [&](const char* n) { return (int)(fn<uint8_t(__cdecl*)(void**)>(n)(&d)); };
  auto setf = fn<void(__cdecl*)(void**, float)>("DLL_HoleDevice_SetSensitivity");
  const float values[] = {0.f, 12.5f, 255.f, 254.95f, 300.f, -5.f, NAN, INFINITY, -INFINITY, 1e-3f};
  for (float v : values) {
    setf(&d, v);
    printf("sens=%08x get=%08x pos=%08x laser=%08x pressure=%08x plane=%08x speed=%08x en=%d\n", bits(v),
      bits(getf("DLL_HoleDevice_GetSensitivity")), bits(getf("DLL_HoleDevice_GetPositon")),
      bits(getf("DLL_HoleDevice_GetLaserPosition")), bits(getf("DLL_HoleDevice_GetPressure")),
      bits(getf("DLL_HoleDevice_GetPlanePosition")), bits(getf("DLL_HoleDevice_GetSecondsSpeed")), getb("DLL_HoleDevice_IsEnabled"));
  }
  printf("arrows=%d,%d,%d,%d buttons=%d,%d,%d,%d\n", geti("DLL_HoleDevice_GetArrowUp"), geti("DLL_HoleDevice_GetArrowDown"),
    geti("DLL_HoleDevice_GetArrowLeft"), geti("DLL_HoleDevice_GetArrowRight"), getb("DLL_HoleDevice_GetFinishButton"),
    getb("DLL_HoleDevice_GetStickButton"), getb("DLL_HoleDevice_GetLeftClickButton"), getb("DLL_HoleDevice_GetRightClickButton"));
  auto start = fn<uint8_t(__cdecl*)(void**)>("DLL_HoleDevice_StartThread");
  auto stop = fn<void(__cdecl*)(void**)>("DLL_HoleDevice_StopThred");
  const int first_start = start(&d);
  const int second_start = start(&d);
  printf("start=%d start2=%d\n", first_start, second_start);
  Sleep(200);
  stop(&d);
  stop(&d);
  printf("restart=%d\n", start(&d));
  Sleep(100);
  printf("after: en=%d pressure=%08x\n", getb("DLL_HoleDevice_IsEnabled"), bits(getf("DLL_HoleDevice_GetPressure")));
  fn<void(__cdecl*)(void**)>("DLL_HoleDevice_Delete")(&d);
  printf("deleted=%d\n", d == nullptr);
  return 0;
}
