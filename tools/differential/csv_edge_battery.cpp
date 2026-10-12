// CSV differential edge battery: real NEI members (read-only files from a directory) plus malformed inputs, boundary coordinates and
// buffer capacities, repeated Open and invalid file objects. Prints deterministic lines so the ORIGINAL and the reconstruction can be diffed.
// Usage: csv_edge_battery.exe <dll> <directory with .nei files> [max files]
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Csv { void* object; };
struct FileH { void* object; };
static HMODULE m;
template <class T> T fn(const char* n) { T p = reinterpret_cast<T>(GetProcAddress(m, n)); if (!p) { std::printf("missing %s\n", n); std::exit(3); } return p; }
static uint64_t fnv(const unsigned char* b, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull; return h; }
static std::vector<unsigned char> slurp(const std::string& p) {
  std::vector<unsigned char> v; HANDLE h = CreateFileA(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return v; LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz); v.resize((size_t)sz.QuadPart); DWORD got = 0; if (!v.empty()) ReadFile(h, v.data(), (DWORD)v.size(), &got, nullptr); CloseHandle(h); return v;
}

static void probe(const char* label, void* csv) {
  auto isValid = fn<uint32_t(__cdecl*)(void*)>("DLL_CSV_IsValid");
  auto maxX = fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellX");
  auto maxY = fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellY");
  const int32_t w = maxX(csv), h = maxY(csv);
  std::printf("%s valid=%u w=%d h=%d\n", label, isValid(csv) & 0xff, w, h);
  const int32_t xs[] = {0, 1, w > 0 ? w - 1 : 0, w, w + 3, 100000, INT32_MAX, -1, INT32_MIN};
  const int32_t ys[] = {0, 1, h > 0 ? h - 1 : 0, h, h + 3, 100000, INT32_MAX, -1, INT32_MIN};
  const int32_t caps[] = {0, 1, 2, 5, 40, -1};
  auto exists = fn<uint32_t(__cdecl*)(void*, int32_t, int32_t)>("DLL_CSV_IsCellToExistData");
  auto asInt = fn<int32_t(__cdecl*)(void*, int32_t, int32_t)>("DLL_CSV_GetCellAsInteger");
  auto asReal = fn<float(__cdecl*)(void*, int32_t, int32_t)>("DLL_CSV_GetCellAsReal");
  auto asBool = fn<uint32_t(__cdecl*)(void*, int32_t, int32_t)>("DLL_CSV_GetCellBool");
  auto sizeS = fn<int32_t(__cdecl*)(void*, int32_t, int32_t)>("DLL_CSV_GetDataSizeString");
  auto sizeB = fn<int32_t(__cdecl*)(void*, int32_t, int32_t)>("DLL_CSV_GetDataSizeBinary");
  auto asStr = fn<void(__cdecl*)(void*, int32_t, int32_t, char*, int32_t)>("DLL_CSV_GetCellAsString");
  auto asBin = fn<void(__cdecl*)(void*, int32_t, int32_t, void*, int32_t)>("DLL_CSV_GetCellAsBinary");
  for (int32_t x : xs) for (int32_t y : ys) {
    // the original reads out of bounds for coordinates outside the table; only probe those the original rejects through its own checks
    const bool inside = x >= 0 && y >= 0 && x < w && y < h;
    const bool beyond = (x >= w || y >= h) && x >= 0 && y >= 0;
    if (!inside && !beyond) continue;
    unsigned e = exists(csv, x, y) & 0xff;
    std::printf("%s (%d,%d) exists=%u int=%d real=%.6g bool=%u sizeS=%d sizeB=%d", label, x, y, e, asInt(csv, x, y), (double)asReal(csv, x, y), asBool(csv, x, y) & 0xff, sizeS(csv, x, y), sizeB(csv, x, y));
    for (int32_t cap : caps) {
      unsigned char sbuf[64]; std::memset(sbuf, 0xCC, sizeof sbuf);
      asStr(csv, x, y, (char*)sbuf, cap);
      unsigned char bbuf[64]; std::memset(bbuf, 0xCC, sizeof bbuf);
      asBin(csv, x, y, bbuf, cap);
      std::printf(" | cap%d s=%016llx b=%016llx", cap, (unsigned long long)fnv(sbuf, sizeof sbuf), (unsigned long long)fnv(bbuf, sizeof bbuf));
    }
    std::printf("\n");
  }
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  if (!m) return 3;
  const std::string dir = argv[2];
  const int maxFiles = argc > 3 ? atoi(argv[3]) : 8;
  auto create = fn<void*(__cdecl*)()>("DLL_CSV_CreateCsvParser");
  auto open = fn<uint32_t(__cdecl*)(void*, void*)>("DLL_CSV_Open");
  auto destroy = fn<void(__cdecl*)(void*)>("DLL_CSV_DeleteCsvParser");
  auto fmCreate = fn<void(__cdecl*)(FileH*)>("DLL_FileMemory_CreateileMemory");
  auto fmSet = fn<void(__cdecl*)(FileH*, const void*, int32_t)>("DLL_FileMemory_SetData");
  auto fmIface = fn<void*(__cdecl*)(FileH*)>("DLL_FileMemory_GetInterfaceFilePointer");
  auto fmDelete = fn<void(__cdecl*)(FileH*)>("DLL_FileMemory_DeleteileMemory");

  WIN32_FIND_DATAA fd; std::vector<std::string> files;
  HANDLE h = FindFirstFileA((dir + "\\*.nei").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) { do files.push_back(fd.cFileName); while (FindNextFileA(h, &fd) && (int)files.size() < maxFiles); FindClose(h); }
  std::printf("files=%d\n", (int)files.size());
  void* csv = create();
  std::printf("fresh: valid=%u w=%d h=%d\n", fn<uint32_t(__cdecl*)(void*)>("DLL_CSV_IsValid")(csv) & 0xff, fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellX")(csv), fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellY")(csv));
  for (const auto& name : files) {
    auto bytes = slurp(dir + "\\" + name);
    FileH f{}; fmCreate(&f); fmSet(&f, bytes.data(), (int32_t)bytes.size());
    const uint32_t r = open(csv, fmIface(&f));
    std::printf("== %s size=%zu open=%u\n", name.c_str(), bytes.size(), r & 0xff);
    probe("nei", csv);
    // Open again with an invalid file object: the previous table must stay.
    FileH bad{}; fmCreate(&bad);
    const uint32_t r2 = open(csv, fmIface(&bad));
    std::printf("reopen invalid: open=%u\n", r2 & 0xff);
    probe("kept", csv);
    fmDelete(&bad);
    // Variations of the same bytes: truncated tails, flipped trailer bytes, zero padding.
    // Sizes with (size - 5) % 16 == 0 and a tiny size make the original compute a negative decode size and crash; they are not probed.
    const size_t sizes[] = {0, 1, 4, 6, 20, 36, 100, bytes.size() > 5 ? bytes.size() - 1 : 0, bytes.size() > 6 ? bytes.size() - 5 : 0};
    for (size_t sz : sizes) {
      if (sz > bytes.size()) continue;
      FileH v{}; fmCreate(&v); fmSet(&v, bytes.data(), (int32_t)sz);
      const uint32_t rv = open(csv, fmIface(&v));
      std::printf("trunc %zu: open=%u valid=%u w=%d h=%d\n", sz, rv & 0xff, fn<uint32_t(__cdecl*)(void*)>("DLL_CSV_IsValid")(csv) & 0xff, fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellX")(csv), fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellY")(csv));
      fmDelete(&v);
    }
    for (int flip = 0; flip < 4 && bytes.size() > 8; ++flip) {
      auto copy = bytes; copy[copy.size() - 1 - flip] ^= 0x5a;
      FileH v{}; fmCreate(&v); fmSet(&v, copy.data(), (int32_t)copy.size());
      const uint32_t rv = open(csv, fmIface(&v));
      std::printf("flip trailer %d: open=%u valid=%u w=%d h=%d\n", flip, rv & 0xff, fn<uint32_t(__cdecl*)(void*)>("DLL_CSV_IsValid")(csv) & 0xff, fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellX")(csv), fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellY")(csv));
      fmDelete(&v);
    }
    // Reload the good bytes and go on.
    fmSet(&f, bytes.data(), (int32_t)bytes.size());
    open(csv, fmIface(&f));
    fmDelete(&f);
  }
  // Garbage files of several sizes.
  uint64_t g = 0x1234abcd5678ef01ull;
  for (int size : {6, 22, 38, 54, 64, 100, 250}) {
    std::vector<unsigned char> junk(size); for (auto& b : junk) { g ^= g << 13; g ^= g >> 7; g ^= g << 17; b = (unsigned char)g; }
    FileH v{}; fmCreate(&v); fmSet(&v, junk.data(), size);
    const uint32_t rv = open(csv, fmIface(&v));
    std::printf("junk %d: open=%u valid=%u w=%d h=%d\n", size, rv & 0xff, fn<uint32_t(__cdecl*)(void*)>("DLL_CSV_IsValid")(csv) & 0xff, fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellX")(csv), fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellY")(csv));
    probe("junk", csv);
    fmDelete(&v);
  }
  destroy(csv);
  return 0;
}
