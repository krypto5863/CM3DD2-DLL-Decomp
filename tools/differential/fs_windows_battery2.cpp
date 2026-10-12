// Second Windows file system battery: base directory + relative folders, trailing slashes/spaces, unicode names, the same file
// name in several sub folders (scan order), auto paths. Original vs rebuild transcripts are diffed.
// Usage: fs_windows_battery2.exe <dll> <work dir>
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct Fs { void* object; int kind; int reserved; };
struct FileH { void* object; };
struct ListH { int32_t count; int32_t reserved; void* strings; };
static HMODULE m;
template <class T> T fn(const char* n) { T p = reinterpret_cast<T>(GetProcAddress(m, n)); if (!p) { std::printf("missing %s\n", n); std::exit(3); } return p; }
static std::wstring wide(const std::string& s, UINT cp) { int n = MultiByteToWideChar(cp, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(cp, 0, s.c_str(), -1, &w[0], n); w.resize(n - 1); return w; }
static std::string utf8(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); s.resize(n - 1); return s; }
static void put(const std::wstring& path, const std::string& text) { HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); DWORD w; WriteFile(h, text.data(), (DWORD)text.size(), &w, nullptr); CloseHandle(h); }
static void mk(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }

static void showList(Fs& fs, const std::string& name, int mode) {
  ListH list{};
  fn<void(__cdecl*)(Fs*, const char*, int, ListH*)>("DLL_FileSystem_CreateList")(&fs, name.c_str(), mode, &list);
  std::printf("CreateList('%s', %d) count=%d\n", name.c_str(), mode, list.count);
  for (int i = 0; i < list.count; ++i) { char b[512] = {}; fn<uint32_t(__cdecl*)(ListH*, int, char*, int)>("DLL_FileSystem_AtList")(&list, i, b, sizeof b); std::printf("  [%d] %s\n", i, b); }
  fn<void(__cdecl*)(ListH*)>("DLL_FileSystem_DeleteList")(&list);
}
static void look(Fs& fs, const std::string& n) {
  const uint32_t e = fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, n.c_str());
  FileH f{};
  const uint32_t g = fn<uint32_t(__cdecl*)(Fs*, const char*, FileH*)>("DLL_FileSystem_GetFile")(&fs, n.c_str(), &f);
  std::string t = "<none>";
  if (g) { char b[64] = {}; auto sz = fn<uint64_t(__cdecl*)(FileH*, void*, uint64_t)>("DLL_File_Read")(&f, b, 63); t.assign(b, (size_t)sz); fn<void(__cdecl*)(FileH*)>("DLL_File_CloseFile")(&f); }
  std::printf("lookup '%s': e=%u g=%u %s\n", n.c_str(), e & 0xff, g & 0xff, t.c_str());
}

int main(int, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  std::wstring work = wide(argv[2], CP_ACP);
  mk(work);
  const std::wstring base = work + L"\\base", jp = L"\u30e1\u30cb\u30e5\u30fc";  // "menu" in katakana
  mk(base); mk(base + L"\\rel1"); mk(base + L"\\rel1\\z_sub"); mk(base + L"\\rel1\\a_sub"); mk(base + L"\\rel1\\a_sub\\inner"); mk(base + L"\\rel2");
  put(base + L"\\rel1\\z_sub\\same.txt", "z"); put(base + L"\\rel1\\a_sub\\same.txt", "a"); put(base + L"\\rel1\\a_sub\\inner\\same.txt", "inner");
  put(base + L"\\rel1\\same.txt", "top"); put(base + L"\\rel1\\" + jp + L".menu", "jp-menu"); put(base + L"\\rel1\\sp ace.txt", "spaced");
  put(base + L"\\rel1\\Mixed.CaSe.Txt", "mixed"); put(base + L"\\rel2\\r2.txt", "r2"); put(base + L"\\rel2\\same.txt", "rel2-same");
  put(base + L"\\rel1\\a_sub\\only-a.txt", "only-a"); put(base + L"\\rel1\\z_sub\\only-z.txt", "only-z");
  Fs fs{};
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemWindows")(&fs);
  std::printf("SetBaseDirectory(relative)=%u\n", fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_SetBaseDirectory")(&fs, "rel") & 0xff);
  std::printf("SetBaseDirectory(base)=%u\n", fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_SetBaseDirectory")(&fs, utf8(base).c_str()) & 0xff);
  auto add = fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddFolder");
  std::printf("AddFolder(rel1)=%u\n", add(&fs, "rel1") & 0xff);
  std::printf("AddFolder(REL1/)=%u\n", add(&fs, "REL1/") & 0xff);
  std::printf("AddFolder(rel1 \\ )=%u\n", add(&fs, "rel1 \\") & 0xff);
  std::printf("AddFolder(rel2)=%u\n", add(&fs, "rel2") & 0xff);
  std::printf("AddFolder(abs rel2)=%u\n", add(&fs, utf8(base + L"\\rel2").c_str()) & 0xff);
  std::printf("AddAutoPath(a_sub)=%u again=%u inner=%u\n", fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddAutoPath")(&fs, "a_sub") & 0xff,
    fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddAutoPath")(&fs, "A_SUB") & 0xff, fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddAutoPath")(&fs, "a_sub\\inner") & 0xff);
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_AddAutoPathForAllFolder")(&fs);
  std::printf("autopath finished=%u\n", fn<uint32_t(__cdecl*)(Fs*)>("DLL_FileSystem_IsFinishedAddAutoPathJob")(&fs) & 0xff);
  const std::string names[] = {"same.txt", "only-a.txt", "only-z.txt", "r2.txt", "mixed.case.txt", "sp ace.txt", utf8(jp + L".menu"), "a_sub\\same.txt", "inner\\same.txt", "a_sub/only-a.txt"};
  for (const auto& n : names) look(fs, n);
  for (int mode = 0; mode <= 3; ++mode) { showList(fs, "", mode); showList(fs, "a_sub", mode); }
  ListH nm{};
  fn<void(__cdecl*)(Fs*, const char*, ListH*)>("DLL_FileSystem_GetNameMapLastArchive")(&fs, nullptr, &nm);
  std::printf("lastNames=%d\n", nm.count);
  for (int i = 0; i < nm.count; ++i) { char b[512] = {}; fn<uint32_t(__cdecl*)(ListH*, int, char*, int)>("DLL_FileSystem_AtList")(&nm, i, b, sizeof b); std::printf("  %s\n", b); }
  const std::string cache1 = utf8(work + L"\\cache_main_") + argv[3] + ".bin";
  fn<void(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_CasheSerialize")(&fs, cache1.c_str());
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&fs);
  // A bulk tree: enough files and sub folders to cross several bucket-count thresholds of the unordered_maps.
  mk(base + L"\\bulk");
  for (int d = 0; d < 40; ++d) {
    wchar_t dir[64]; swprintf(dir, 64, L"\\bulk\\d%02d", d); mk(base + dir);
    for (int f = 0; f < 9; ++f) { wchar_t fn2[96]; swprintf(fn2, 96, L"\\bulk\\d%02d\\file_%02d_%d.ext%d", d, d, f, f % 3); put(base + fn2, "bulk"); }
    wchar_t dup[96]; swprintf(dup, 96, L"\\bulk\\d%02d\\shared.txt", d); put(base + dup, "x");
  }
  Fs bulk{};
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemWindows")(&bulk);
  std::printf("bulk add=%u\n", add(&bulk, utf8(base + L"\\bulk").c_str()) & 0xff);
  const std::string cache2 = utf8(work + L"\\cache_bulk_") + argv[3] + ".bin";
  fn<void(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_CasheSerialize")(&bulk, cache2.c_str());
  showList(bulk, "", 2);
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&bulk);
  // A file system with a single root: the first file in scan order wins among same-named files of its sub folders.
  Fs one{};
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemWindows")(&one);
  std::printf("single root add=%u\n", add(&one, utf8(base + L"\\rel1").c_str()) & 0xff);
  look(one, "same.txt"); look(one, "only-a.txt"); look(one, "only-z.txt");
  fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&one);
  return 0;
}
