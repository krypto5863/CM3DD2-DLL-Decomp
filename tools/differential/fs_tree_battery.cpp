// Tree-mode (flag set) battery. Two backends run the SAME scenario and print the SAME transcript:
//   * the ORIGINAL cm3d2.dll: the native FileSystemWindows keeps its scanner mode flag at object +0x190; the virtual-base subobject at
//     object +0x1e0 (vtable RVA 0x1dd150, slot 2 = get, slot 3 = set) flips it without any new export. With the flag set AddFolder
//     builds a node tree (FUN_18007bb80 + FUN_18007e6e0) and CreateList/GetFile/CasheSerialize use it.
//   * the reconstruction's WindowsFolderSet (compile with -DTREE_BATTERY_INTERNAL and link src/file_system_windows.cpp,
//     src/msvc_ordered_map.cpp, src/warc_archive.cpp): setTreeMode(true) plus the same operations.
// Usage:  fs_tree_battery.exe <original dll> <work dir> <tag>        (DLL backend)
//         fs_tree_internal.exe <work dir> <tag>                      (internal backend)
// Both write tree_cache_<tag>.bin into <work dir>; run them in turn with the same <work dir> and compare transcripts and cache bytes.
// SAFE: the tree lives in <work dir>. Never point the original at game archives (warp patches get rewritten).
// NOT covered: AddAutoPathForAllFolder (the native job did not return in tree mode within 300 s).
#include <Windows.h>

#include <cstdarg>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef TREE_BATTERY_INTERNAL
#include "../../src/file_system_windows.h"
#endif

static std::string g_transcript;
static void emit(const char* format, ...) {
  char buffer[2048];
  va_list args; va_start(args, format);
  std::vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  std::fputs(buffer, stdout);
  g_transcript += buffer;
}
static std::wstring wide(const std::string& s, UINT cp) { int n = MultiByteToWideChar(cp, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(cp, 0, s.c_str(), -1, &w[0], n); w.resize(n - 1); return w; }
static std::string utf8(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); s.resize(n - 1); return s; }
static void put(const std::wstring& path, const std::string& text) { HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); DWORD w; WriteFile(h, text.data(), (DWORD)text.size(), &w, nullptr); CloseHandle(h); }
static void mk(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }
static const wchar_t B = L'\\';
static std::wstring J(std::initializer_list<std::wstring> parts) { std::wstring r; for (auto& p : parts) { if (!r.empty()) r += B; r += p; } return r; }

// ---- backend interface -------------------------------------------------------------------------------------------------------
struct Backend {
  virtual ~Backend() {}
  virtual void create(bool tree) = 0;
  virtual unsigned setBase(const std::string& path) = 0;
  virtual unsigned addFolder(const std::string& name) = 0;
  virtual unsigned addAutoPath(const std::string& name) = 0;
  virtual bool exists(const std::string& name) = 0;
  virtual bool read(const std::string& name, std::string& content) = 0;
  virtual std::vector<std::string> list(const std::string& name, int mode) = 0;
  virtual std::vector<std::string> extList(const std::string& ext) = 0;
  virtual std::vector<std::string> lastNames() = 0;
  virtual void serialize(const std::string& path) = 0;
  virtual unsigned deserialize(const std::string& path) = 0;
  virtual void destroy() = 0;
};

#ifndef TREE_BATTERY_INTERNAL
struct Fs { void* object; int kind; int reserved; };
struct FileH { void* object; };
struct ListH { int32_t count; int32_t reserved; void* strings; };
static HMODULE m;
template <class T> T fn(const char* n) { T p = reinterpret_cast<T>(GetProcAddress(m, n)); if (!p) { emit("missing %s\n", n); std::exit(3); } return p; }
struct DllBackend : Backend {
  Fs fs{};
  std::vector<std::string> take(ListH& list) {
    std::vector<std::string> out;
    for (int i = 0; i < list.count; ++i) { char b[512] = {}; fn<uint32_t(__cdecl*)(ListH*, int, char*, int)>("DLL_FileSystem_AtList")(&list, i, b, sizeof b); out.push_back(b); }
    fn<void(__cdecl*)(ListH*)>("DLL_FileSystem_DeleteList")(&list);
    return out;
  }
  void create(bool tree) override {
    fs = Fs{};
    fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemWindows")(&fs);
    char* vb = (char*)fs.object + 0x1e0;
    void** vt = *(void***)vb;
    typedef unsigned char (__cdecl* Set)(void*, unsigned char);
    ((Set)vt[3])(vb, tree ? 1 : 0);
  }
  unsigned setBase(const std::string& p) override { return fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_SetBaseDirectory")(&fs, p.c_str()) & 0xff; }
  unsigned addFolder(const std::string& n) override { return fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddFolder")(&fs, n.c_str()) & 0xff; }
  unsigned addAutoPath(const std::string& n) override { return fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddAutoPath")(&fs, n.c_str()) & 0xff; }
  bool exists(const std::string& n) override { return (fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, n.c_str()) & 0xff) != 0; }
  bool read(const std::string& n, std::string& t) override {
    FileH f{};
    if (!(fn<uint32_t(__cdecl*)(Fs*, const char*, FileH*)>("DLL_FileSystem_GetFile")(&fs, n.c_str(), &f) & 0xff)) return false;
    char b[64] = {}; auto sz = fn<uint64_t(__cdecl*)(FileH*, void*, uint64_t)>("DLL_File_Read")(&f, b, 63); t.assign(b, (size_t)sz);
    fn<void(__cdecl*)(FileH*)>("DLL_File_CloseFile")(&f);
    return true;
  }
  std::vector<std::string> list(const std::string& n, int mode) override { ListH l{}; fn<void(__cdecl*)(Fs*, const char*, int, ListH*)>("DLL_FileSystem_CreateList")(&fs, n.c_str(), mode, &l); return take(l); }
  std::vector<std::string> extList(const std::string& e) override { ListH l{}; fn<void(__cdecl*)(Fs*, const char*, ListH*)>("DLL_FileSystem_CreateExtensionList")(&fs, e.c_str(), &l); return take(l); }
  std::vector<std::string> lastNames() override { ListH l{}; fn<void(__cdecl*)(Fs*, const char*, ListH*)>("DLL_FileSystem_GetNameMapLastArchive")(&fs, nullptr, &l); return take(l); }
  void serialize(const std::string& p) override { fn<void(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_CasheSerialize")(&fs, p.c_str()); }
  unsigned deserialize(const std::string& p) override { return fn<uint32_t(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_CasheDeserialize")(&fs, p.c_str()) & 0xff; }
  void destroy() override { fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&fs); }
};
#else
using cm3d2::internal::WindowsFolderSet;
using cm3d2::internal::normalizeFileSystemName;
struct InternalBackend : Backend {
  std::unique_ptr<WindowsFolderSet> set;
  std::wstring base;
  void create(bool tree) override { set = std::make_unique<WindowsFolderSet>(); set->setTreeMode(tree); base.clear(); }
  unsigned setBase(const std::string& p) override {
    std::wstring n = wide(p, CP_UTF8);
    if (n.size() < 2 || n.find(L':', 1) == std::wstring::npos) return 0;
    base = normalizeFileSystemName(n) + L"\\";
    return 1;
  }
  unsigned addFolder(const std::string& n) override { return set->addFolder(wide(n, CP_UTF8), base) ? 1 : 0; }
  unsigned addAutoPath(const std::string& n) override { return set->addAutoPath(wide(n, CP_UTF8)) ? 1 : 0; }
  bool exists(const std::string& n) override {
    const std::wstring p = set->resolve(wide(n, CP_UTF8));
    if (p.empty()) return false;
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
  }
  bool read(const std::string& n, std::string& t) override {
    const std::wstring p = set->resolve(wide(n, CP_UTF8));
    if (p.empty()) return false;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char b[64] = {}; DWORD got = 0; ReadFile(h, b, 63, &got, nullptr); CloseHandle(h);
    t.assign(b, got);
    return true;
  }
  std::vector<std::string> list(const std::string& n, int mode) override { std::vector<std::string> o; for (auto& w : set->list(wide(n, CP_UTF8), mode)) o.push_back(utf8(w)); return o; }
  std::vector<std::string> extList(const std::string& e) override {
    std::vector<std::string> o;
    for (auto& w : set->list(L"", 3)) {
      std::string name = utf8(w);
      if (e == "*") { o.push_back(name); continue; }
      const auto dot = name.rfind('.');
      std::string x = dot == std::string::npos ? std::string() : name.substr(dot + 1);
      while (!x.empty() && x.back() == ' ') x.pop_back();
      if (x == e) o.push_back(name);
    }
    return o;
  }
  std::vector<std::string> lastNames() override { std::vector<std::string> o; for (auto& w : set->fileNameMap()) o.push_back(utf8(w)); return o; }
  void serialize(const std::string& p) override { set->serialize(wide(p, CP_UTF8), base); }
  unsigned deserialize(const std::string& p) override { std::wstring b = base; return set->deserialize(wide(p, CP_UTF8), b) ? 1 : 0; }
  void destroy() override { set.reset(); }
};
#endif

// ---- scenario ----------------------------------------------------------------------------------------------------------------
static void showList(Backend& b, const std::string& name, int mode) {
  auto l = b.list(name, mode);
  emit("CreateList('%s', %d) count=%d\n", name.c_str(), mode, (int)l.size());
  for (size_t i = 0; i < l.size(); ++i) emit("  [%d] %s\n", (int)i, l[i].c_str());
}
static void showExt(Backend& b, const char* ext) {
  auto l = b.extList(ext);
  emit("CreateExtensionList('%s') count=%d\n", ext, (int)l.size());
  for (size_t i = 0; i < l.size(); ++i) emit("  [%d] %s\n", (int)i, l[i].c_str());
}
static void look(Backend& b, const std::string& n) {
  const bool e = b.exists(n);
  std::string t = "<none>";
  const bool g = b.read(n, t);
  if (!g) t = "<none>";
  emit("lookup '%s': e=%u g=%u %s\n", n.c_str(), e ? 1u : 0u, g ? 1u : 0u, t.c_str());
}
static void names(Backend& b) {
  const char* all[] = {"same.txt", "only-a.txt", "only-z.txt", "only-inner.txt", "r2.txt", "mixed.case.txt", "sp ace.txt", "a_sub\\same.txt", "a_sub/same.txt", "A_SUB\\Same.TXT",
    "z_sub\\same.txt", "a_sub\\inner\\same.txt", "inner\\same.txt", "a_sub\\only-a.txt", "a_sub\\only-z.txt", "z_sub\\only-z.txt", "a_sub\\inner\\only-inner.txt", "rel1\\a_sub\\same.txt",
    "missing\\same.txt", "a_sub\\missing.txt", "a_sub", "a_sub\\", "\\same.txt", "a_sub\\inner\\", "keep.d\\kept.txt", "kept.txt", "hidden.txt", ".works1\\hidden.txt"};
  for (const char* n : all) look(b, n);
}
static void allLists(Backend& b) {
  const char* listNames[] = {"", "*", "a_sub", "a_sub\\", "A_SUB/inner", "a_sub\\inner", "z_sub", "keep.d", "missing", "a_sub\\missing", "rel1", "inner"};
  for (const char* n : listNames) for (int mode = 0; mode <= 3; ++mode) showList(b, n, mode);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef TREE_BATTERY_INTERNAL
  if (argc < 3) return 2;
  InternalBackend backend;
  const std::wstring work = wide(argv[1], CP_ACP);
  const std::string tag = argv[2];
#else
  if (argc < 4) return 2;
  m = LoadLibraryA(argv[1]);
  if (!m) return 3;
  DllBackend backend;
  const std::wstring work = wide(argv[2], CP_ACP);
  const std::string tag = argv[3];
#endif
  Backend& b = backend;
  mk(work);
  const std::wstring base = J({work, L"base"});
  mk(base);
  const std::wstring r1 = J({base, L"rel1"}), r2 = J({base, L"rel2"});
  mk(r1); mk(J({r1, L"z_sub"})); mk(J({r1, L"a_sub"})); mk(J({r1, L"a_sub", L"inner"})); mk(J({r1, L".works1"})); mk(J({r1, L"keep.d"})); mk(r2); mk(J({r2, L"a_sub"}));
  put(J({r1, L"z_sub", L"same.txt"}), "z"); put(J({r1, L"a_sub", L"same.txt"}), "a"); put(J({r1, L"a_sub", L"inner", L"same.txt"}), "inner"); put(J({r1, L"same.txt"}), "top");
  put(J({r1, L"sp ace.txt"}), "spaced"); put(J({r1, L"Mixed.CaSe.Txt"}), "mixed"); put(J({r1, L"a_sub", L"only-a.txt"}), "only-a"); put(J({r1, L"z_sub", L"only-z.txt"}), "only-z");
  put(J({r1, L"a_sub", L"inner", L"only-inner.txt"}), "only-inner"); put(J({r1, L".works1", L"hidden.txt"}), "hidden"); put(J({r1, L"keep.d", L"kept.txt"}), "kept");
  put(J({r2, L"r2.txt"}), "r2"); put(J({r2, L"same.txt"}), "rel2-same"); put(J({r2, L"a_sub", L"same.txt"}), "rel2-a-same"); put(J({r2, L"a_sub", L"only-r2a.txt"}), "r2-only-a");
  b.create(true);
  emit("tree mode set\n");
  emit("SetBaseDirectory=%u\n", b.setBase(utf8(base)));
  emit("AddFolder(rel1)=%u\n", b.addFolder("rel1"));
  emit("AddFolder(rel2)=%u\n", b.addFolder("rel2"));
  emit("AddFolder(rel1 again)=%u\n", b.addFolder("REL1/"));
  emit("-- before auto paths\n");
  names(b);
  emit("AddAutoPath(a_sub)=%u\n", b.addAutoPath("a_sub"));
  emit("AddAutoPath(A_SUB again)=%u\n", b.addAutoPath("A_SUB"));
  emit("AddAutoPath(a_sub\\inner)=%u\n", b.addAutoPath("a_sub\\inner"));
  emit("AddAutoPath(z_sub)=%u\n", b.addAutoPath("z_sub"));
  emit("AddAutoPath(nope)=%u\n", b.addAutoPath("nope"));
  emit("-- after auto paths\n");
  names(b);
  allLists(b);
  showExt(b, "txt"); showExt(b, "*"); showExt(b, "d");
  {
    auto l = b.lastNames();
    emit("lastNames=%d\n", (int)l.size());
    for (auto& s : l) emit("  %s\n", s.c_str());
  }
  const std::string cache = utf8(J({work, L"tree_cache_"})) + tag + ".bin";
  b.serialize(cache);
  emit("serialized\n");
  b.destroy();
  // ---- scenario 2: rehash pressure, deep chains and the resolver quirks -------------------------------------------------------
  const std::wstring bulk = J({base, L"bulk"});
  mk(bulk);
  for (int d = 0; d < 14; ++d) {
    wchar_t dn[16]; swprintf(dn, 16, L"d%02d", d);
    const std::wstring dir = J({bulk, dn}); mk(dir); mk(J({dir, L"x"})); mk(J({dir, L"x", L"y"}));
    for (int f = 0; f < 6; ++f) { wchar_t fnm[32]; swprintf(fnm, 32, L"f%d_%02d.ext%d", f, d, f % 3); put(J({dir, fnm}), "bulk"); }
    put(J({dir, L"same.dat"}), std::string("same-") + std::to_string(d)); put(J({dir, L"x", L"same.dat"}), std::string("x-") + std::to_string(d)); put(J({dir, L"x", L"y", L"leaf.txt"}), std::string("leaf-") + std::to_string(d));
  }
  const std::wstring quirk = J({base, L"quirk"});
  mk(quirk); mk(J({quirk, L"p"})); mk(J({quirk, L"p", L"q"})); mk(J({quirk, L"q"})); mk(J({quirk, L"q", L"z"}));
  put(J({quirk, L"q", L"f.txt"}), "root-q-f"); put(J({quirk, L"q", L"z", L"f2.txt"}), "root-qz-f2"); put(J({quirk, L"p", L"q", L"other.txt"}), "p-q-other"); put(J({quirk, L"top.txt"}), "top");
  b.create(true);
  emit("== scenario 2\n");
  emit("SetBaseDirectory=%u\n", b.setBase(utf8(base)));
  emit("AddFolder(bulk)=%u\n", b.addFolder("bulk"));
  emit("AddFolder(quirk)=%u\n", b.addFolder("quirk"));
  emit("AddAutoPath(d03)=%u\n", b.addAutoPath("d03"));
  emit("AddAutoPath(d03 x)=%u\n", b.addAutoPath("d03\\x"));
  emit("AddAutoPath(d11 x y)=%u\n", b.addAutoPath("d11\\x\\y"));
  emit("AddAutoPath(p)=%u\n", b.addAutoPath("p"));
  const char* probes[] = {"same.dat", "x\\same.dat", "y\\leaf.txt", "d03\\same.dat", "d05\\x\\same.dat", "d13\\x\\y\\leaf.txt", "d03\\x\\y\\leaf.txt", "x\\y\\leaf.txt", "f2_07.ext2", "d07\\f2_07.ext2",
    "q\\f.txt", "q\\z\\f2.txt", "p\\q\\other.txt", "q\\other.txt", "top.txt", "p\\top.txt"};
  for (const char* n : probes) look(b, n);
  const char* names2[] = {"", "d03", "d03\\x", "x", "y", "q", "p", "p\\q"};
  for (const char* n : names2) for (int mode = 0; mode <= 3; ++mode) showList(b, n, mode);
  showExt(b, "ext1"); showExt(b, "dat"); showExt(b, "txt");
  const std::string cache2 = utf8(J({work, L"tree_cache2_"})) + tag + ".bin";
  b.serialize(cache2);
  b.destroy();
  b.create(false);
  emit("Deserialize2=%u\n", b.deserialize(cache2));
  for (const char* n : probes) look(b, n);
  const char* names3[] = {"", "d03", "x", "q", "p"};
  for (const char* n : names3) for (int mode = 0; mode <= 3; ++mode) showList(b, n, mode);
  b.destroy();
  // Load the tree cache into a fresh file system (flag clear, as a game would) and repeat lookups and lists.
  b.create(false);
  emit("Deserialize=%u\n", b.deserialize(cache));
  emit("-- loaded\n");
  names(b);
  allLists(b);
  b.destroy();
#ifdef TREE_BATTERY_INTERNAL
  // Optional third argument: a golden transcript captured from the original; the run fails when it differs.
  if (argc > 3) {
    FILE* f = std::fopen(argv[3], "rb");
    if (!f) { std::fputs("golden transcript missing", stderr); return 3; }
    std::string golden; char chunk[4096]; size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) golden.append(chunk, got);
    std::fclose(f);
    // Compare ignoring CR so a checkout with CRLF conversion still matches.
    auto strip = [](std::string t) { std::string r; for (char c : t) if (c != 13) r.push_back(c); return r; };
    if (strip(golden) != strip(g_transcript)) { std::fputs("tree transcript differs from the golden one", stderr); return 1; }
    std::fputs("fs_tree_contract ok", stderr);
  }
#endif
  return 0;
}
