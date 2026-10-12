// The exports no other probe names: TestFlag, the add-auto-path job trio, File_ReleaseInstanceCount, HoleDevice
// CheckHidDevice / UpdateFrame (no hardware needed) and KAG GetKey / ClearScnearioCache.
// Usage: misc_exports_probe.exe <dll> <scratch folder with a few sub folders is created inside>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

struct FsHandle { void* object; int32_t kind; int32_t reserved; };
static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }

int main(int, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  const std::string root = argv[2];
  CreateDirectoryA(root.c_str(), nullptr);
  for (const char* sub : {"a", "b", "a\c"}) CreateDirectoryA((root + "\\" + sub).c_str(), nullptr);
  for (const char* file : {"a\one.txt", "b\two.txt", "a\c\three.txt"}) {
    HANDLE h = CreateFileA((root + "\\" + file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    CloseHandle(h);
  }
  std::printf("TestFlag=%d\n", fn<uint8_t(__cdecl*)()>("DLL_FileSystem_TestFlag")());
  for (int kind = 0; kind < 2; ++kind) {
    FsHandle fs{};
    fn<void(__cdecl*)(FsHandle*)>(kind == 0 ? "DLL_FileSystem_CreateFileSystemWindows" : "DLL_FileSystem_CreateFileSystemArchive")(&fs);
    if (kind == 0) std::printf("addfolder=%d\n", fn<uint8_t(__cdecl*)(FsHandle*, const char*)>("DLL_FileSystem_AddFolder")(&fs, root.c_str()));
    std::printf("kind%d finished(wait0)=%d finished(wait1)=%d\n", kind,
      fn<uint8_t(__cdecl*)(FsHandle*, uint8_t)>("DLL_FileSystem_IsFinishedAddAutoPathJob")(&fs, 0),
      fn<uint8_t(__cdecl*)(FsHandle*, uint8_t)>("DLL_FileSystem_IsFinishedAddAutoPathJob")(&fs, 1));
    fn<void(__cdecl*)(FsHandle*)>("DLL_FileSystem_AddAutoPathForAllFolder")(&fs);
    std::printf("kind%d after AddAutoPathForAllFolder finished=%d valid=%d one=%d two=%d three=%d\n", kind,
      fn<uint8_t(__cdecl*)(FsHandle*, uint8_t)>("DLL_FileSystem_IsFinishedAddAutoPathJob")(&fs, 1),
      fn<uint8_t(__cdecl*)(FsHandle*)>("DLL_FileSystem_IsValid")(&fs),
      fn<uint8_t(__cdecl*)(FsHandle*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, "one.txt"),
      fn<uint8_t(__cdecl*)(FsHandle*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, "two.txt"),
      fn<uint8_t(__cdecl*)(FsHandle*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, "three.txt"));
    fn<void(__cdecl*)(FsHandle*)>("DLL_FileSystem_ReleaseAddAutoPathJob")(&fs);
    std::printf("kind%d released finished=%d\n", kind, fn<uint8_t(__cdecl*)(FsHandle*, uint8_t)>("DLL_FileSystem_IsFinishedAddAutoPathJob")(&fs, 0));
    fn<void(__cdecl*)(FsHandle*)>("DLL_FileSystem_DeleteFileSystem")(&fs);
  }
  fn<void(__cdecl*)()>("DLL_File_ReleaseInstanceCount")();
  std::printf("ReleaseInstanceCount done\n");

  void* device = nullptr;
  fn<void(__cdecl*)(void**)>("DLL_HoleDevice_Create")(&device);
  fn<void(__cdecl*)(void**)>("DLL_HoleDevice_CheckHidDevice")(&device);
  fn<void(__cdecl*)(void**)>("DLL_HoleDevice_UpdateFrame")(&device);
  std::printf("hole enabled=%d pressure=%f position=%f laser=%f sens=%f\n",
    fn<uint8_t(__cdecl*)(void**)>("DLL_HoleDevice_IsEnabled")(&device), fn<float(__cdecl*)(void**)>("DLL_HoleDevice_GetPressure")(&device),
    fn<float(__cdecl*)(void**)>("DLL_HoleDevice_GetPositon")(&device), fn<float(__cdecl*)(void**)>("DLL_HoleDevice_GetLaserPosition")(&device),
    fn<float(__cdecl*)(void**)>("DLL_HoleDevice_GetSensitivity")(&device));
  fn<void(__cdecl*)(void**)>("DLL_HoleDevice_Delete")(&device);

  for (const char* name : {"", "a", "tch", "Tch", "scenarioStr", "\xe3\x82\xab"}) {
    std::printf("GetKey('%s')=%016llx\n", name, static_cast<unsigned long long>(fn<uint64_t(__cdecl*)(void*, const char*)>("DLL_KAG_GetKey")(nullptr, name)));
  }
  fn<void(__cdecl*)()>("DLL_KAG_ClearScnearioCache")();
  std::printf("ClearScnearioCache done\n");
  return 0;
}
