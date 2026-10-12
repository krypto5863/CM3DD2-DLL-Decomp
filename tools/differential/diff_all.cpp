// Differential harness: loads a cm3d2 DLL by path and runs one named battery
// against its exports, printing deterministic lines. Run the same battery against
// the original game DLL and the reconstruction, then diff.
//   diff_all.exe <dll> <battery>
// Batteries: tjs, enum, anm, csv <nei path>, fsd, fsdvalid
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct KeyFrame { float time, value, in_tangent, out_tangent; };

HMODULE g_module = nullptr;

template <typename T>
T fn(const char* name) {
    FARPROC p = GetProcAddress(g_module, name);
    if (p == nullptr) {
        std::printf("MISSING %s\n", name);
        std::exit(3);
    }
    return reinterpret_cast<T>(p);
}

using Void = void;
using VPP = void**;

// ---- TJSVariant --------------------------------------------------------------
void observeTjs(const char* label, void*& v) {
    auto as_string = fn<void(__cdecl*)(VPP, char*, int32_t)>("DLL_TJSVariant_AsString");
    auto as_int = fn<int64_t(__cdecl*)(VPP)>("DLL_TJSVariant_AsInteger");
    auto as_real = fn<float(__cdecl*)(VPP)>("DLL_TJSVariant_AsReal");
    auto type = fn<int32_t(__cdecl*)(VPP)>("DLL_TJSVariant_GetType");
    char buf[96];
    std::memset(buf, 0, sizeof(buf));
    as_string(&v, buf, sizeof(buf));
    std::printf("%-28s type=%d int=%lld real=%.9g str=\"%s\"\n", label, type(&v),
        static_cast<long long>(as_int(&v)), static_cast<double>(as_real(&v)), buf);
}

void batteryTjs() {
    auto create = fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Create");
    auto del = fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Delete");
    auto set_int = fn<void(__cdecl*)(VPP, int32_t)>("DLL_TJSVariant_SetInteger");
    auto set_real = fn<void(__cdecl*)(VPP, float)>("DLL_TJSVariant_SetReal");
    auto set_string = fn<void(__cdecl*)(VPP, const char*)>("DLL_TJSVariant_SetString");
    const char* strings[] = {
        "123", "-5", "+7", "1.5", "-2.75", "0x1F", "0X1f", "010", "true", "false", "NaN", "Infinity",
        "-Infinity", "1e3", "  42", "", "abc", "12abc", "1_000", "0.5", "-0", "9999999999999",
        "- 5", "+ 7", "-\t5", ".5", "1.", "1e", "1e+2", "1E-2", "0b101", "0B11", "0o7", "07", "08", "0.",
        "0e1", "0x", "0xG", "0x10z", "  -3", "false ", "true\t", "truex", "NaNx", "-", "+", "-abc",
        "1,5", "0_1", "1.5e3x", "9223372036854775807", "9223372036854775808", "0xFFFFFFFFFFFFFFFF",
        "1e400", "-0.0", "1.5.5", "00012", "+-5", "--5", "-+5", "e5", "1e5.5", "truefalse", "Infinit",
    };
    for (const char* s : strings) {
        void* v = nullptr;
        create(&v);
        set_string(&v, s);
        char label[96];
        std::snprintf(label, sizeof(label), "string[%s]", s);
        observeTjs(label, v);
        del(&v);
    }
    const int32_t ints[] = {0, -1, 42, 2147483647, -2147483647 - 1};
    for (int32_t i : ints) {
        void* v = nullptr;
        create(&v);
        set_int(&v, i);
        char label[64];
        std::snprintf(label, sizeof(label), "int[%d]", i);
        observeTjs(label, v);
        del(&v);
    }
    const float reals[] = {0.0f, 1.5f, -2.75f, 0.1f, 1.0e-7f, 1.0e20f, 123456.0f, 3.0e38f};
    for (float r : reals) {
        void* v = nullptr;
        create(&v);
        set_real(&v, r);
        char label[64];
        std::snprintf(label, sizeof(label), "real[%g]", static_cast<double>(r));
        observeTjs(label, v);
        del(&v);
    }
    const uint32_t bits[] = {0x80000000u, 0x7f800000u, 0xff800000u, 0x7fc00000u};
    for (uint32_t b : bits) {
        float r;
        std::memcpy(&r, &b, sizeof(r));
        void* v = nullptr;
        create(&v);
        set_real(&v, r);
        char label[64];
        std::snprintf(label, sizeof(label), "bits[%08x]", b);
        observeTjs(label, v);
        del(&v);
    }
    void* v = nullptr;
    create(&v);
    observeTjs("void", v);
    set_string(&v, "abcdef");
    char tiny[3];
    std::memset(tiny, 'Z', sizeof(tiny));
    fn<void(__cdecl*)(VPP, char*, int32_t)>("DLL_TJSVariant_AsString")(&v, tiny, sizeof(tiny));
    for (int i = 0; i < 3; ++i) std::printf("cap2[%d]=%d\n", i, static_cast<int>(static_cast<unsigned char>(tiny[i])));
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Clear")(&v);
    observeTjs("cleared", v);
    del(&v);
}

// ---- EnumData ----------------------------------------------------------------
void batteryEnum() {
    auto create = fn<void*(__cdecl*)()>("DLL_EnumData_CreateEnumData");
    auto del = fn<void(__cdecl*)(void*)>("DLL_EnumData_DeleteEnumData");
    auto add = fn<uint8_t(__cdecl*)(void*, int32_t, const char*)>("DLL_EnumData_AddMember");
    auto rm_text = fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_EnumData_RemoveMenberAtText");
    auto rm_num = fn<uint8_t(__cdecl*)(void*, int32_t)>("DLL_EnumData_RemoveMenberAtNumber");
    void* d = create();
    std::printf("add(1,one)=%d\n", static_cast<int>(add(d, 1, "one")));
    std::printf("add(2,two)=%d\n", static_cast<int>(add(d, 2, "two")));
    std::printf("add(1,uno)=%d\n", static_cast<int>(add(d, 1, "uno")));
    std::printf("add(3,one)=%d\n", static_cast<int>(add(d, 3, "one")));
    std::printf("add(3,three)=%d\n", static_cast<int>(add(d, 3, "three")));
    std::printf("add(4,empty)=%d\n", static_cast<int>(add(d, 4, "")));
    std::printf("add(5,empty)=%d\n", static_cast<int>(add(d, 5, "")));
    std::printf("rmtext(one)=%d\n", static_cast<int>(rm_text(d, "one")));
    std::printf("rmtext(one)=%d\n", static_cast<int>(rm_text(d, "one")));
    std::printf("add(1,uno)=%d\n", static_cast<int>(add(d, 1, "uno")));
    std::printf("add(9,one)=%d\n", static_cast<int>(add(d, 9, "one")));
    std::printf("rmnum(2)=%d\n", static_cast<int>(rm_num(d, 2)));
    std::printf("rmnum(2)=%d\n", static_cast<int>(rm_num(d, 2)));
    std::printf("add(7,two)=%d\n", static_cast<int>(add(d, 7, "two")));
    std::printf("add(2,dos)=%d\n", static_cast<int>(add(d, 2, "dos")));
    std::printf("rmnum(100)=%d\n", static_cast<int>(rm_num(d, 100)));
    std::printf("rmtext(absent)=%d\n", static_cast<int>(rm_text(d, "absent")));
    std::printf("add(-5,negative)=%d\n", static_cast<int>(add(d, -5, "negative")));
    std::printf("rmnum(-5)=%d\n", static_cast<int>(rm_num(d, -5)));
    std::printf("add(min,min)=%d\n", static_cast<int>(add(d, INT32_MIN, "min")));
    std::printf("rmtext(min)=%d\n", static_cast<int>(rm_text(d, "min")));
    del(d);
    std::printf("DONE\n");
}

// ---- AnmParse ----------------------------------------------------------------
void putWord(std::vector<uint8_t>& out, uint32_t v) {
    for (int s = 0; s < 32; s += 8) out.push_back(static_cast<uint8_t>(v >> s));
}
void putString(std::vector<uint8_t>& out, const std::string& text) {
    out.push_back(static_cast<uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}
void putFloat(std::vector<uint8_t>& out, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    putWord(out, bits);
}
void putCurve(std::vector<uint8_t>& out, const std::string& path, uint8_t tag, int frames) {
    out.push_back(1);
    putString(out, path);
    out.push_back(tag);
    putWord(out, static_cast<uint32_t>(frames));
    for (int i = 0; i < frames; ++i) {
        putFloat(out, static_cast<float>(i));
        putFloat(out, static_cast<float>(i) * 2.0f);
        putFloat(out, 0.5f);
        putFloat(out, -0.5f);
    }
}
std::vector<uint8_t> anmHeader() {
    std::vector<uint8_t> out;
    putString(out, "CM3D2_ANIM");
    putWord(out, 1001);
    return out;
}

void dumpAnm(const std::vector<uint8_t>& data, uint8_t l, uint8_t r, uint8_t all, const char* label) {
    auto create = fn<void(__cdecl*)(VPP)>("DLL_AnmParse_New");
    auto del = fn<void(__cdecl*)(VPP)>("DLL_AnmParse_Delete");
    auto open = fn<uint8_t(__cdecl*)(VPP, const uint8_t*, int32_t, uint8_t, uint8_t, uint8_t)>("DLL_AnmParse_OpenByte");
    auto list_of = fn<void*(__cdecl*)(VPP)>("DLL_AnmParse_GetAnimationCurveDataList");
    auto list_size = fn<int64_t(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveDataList_Size");
    auto list_at = fn<void*(__cdecl*)(void*, int32_t)>("DLL_AnmParse_AnimationCurveDataList_At");
    auto get_path = fn<const char*(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveData_GetPath");
    auto com_id = fn<uint32_t(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveData_ComID");
    auto frames_of = fn<void*(__cdecl*)(void*)>("DLL_AnmParse_AnimationCurveData_KeyFrameList");
    auto frames_size = fn<int64_t(__cdecl*)(void*)>("DLL_AnmParse_KeyFrameList_Size");
    auto frame_at = fn<void(__cdecl*)(void*, int32_t, KeyFrame*)>("DLL_AnmParse_KeyFrameList_At");

    void* parser = nullptr;
    create(&parser);
    const uint8_t ok = open(&parser, data.data(), static_cast<int32_t>(data.size()), l, r, all);
    std::printf("%s ok=%d", label, ok);
    if (ok) {
        void* list = list_of(&parser);
        const int64_t n = list_size(list);
        std::printf(" count=%lld\n", static_cast<long long>(n));
        for (int32_t i = 0; i < n; ++i) {
            void* curve = list_at(list, i);
            std::printf("  [%d] path=\"%s\" id=%u", i, get_path(curve), com_id(curve));
            void* frames = frames_of(curve);
            const int64_t fn_count = frames_size(frames);
            std::printf(" frames=%lld\n", static_cast<long long>(fn_count));
            for (int32_t f = 0; f < fn_count; ++f) {
                KeyFrame kf{};
                frame_at(frames, f, &kf);
                std::printf("    f%d %g %g %g %g\n", f, kf.time, kf.value, kf.in_tangent, kf.out_tangent);
            }
        }
    } else {
        std::printf("\n");
    }
    del(&parser);
}

void batteryAnm() {
    std::vector<uint8_t> basic = anmHeader();
    putCurve(basic, "Bip01/Bip01 Pelvis", 100, 2);
    basic.push_back(7);
    putCurve(basic, "Other", 102, 1);
    basic.push_back(0);
    dumpAnm(basic, 0, 0, 0, "basic");

    const char* paths[] = {"Foo/Bar", "Root/Bip01", "Root/Bip01/Spine", "A/_IK_hand", "A/ManBip",
        "A/ManBip/B", "A/Bone_Face/B", "", "Bip01/Mune_L", "Bip01/Mune_R", "Bip01/_IK_Mune_L"};
    const uint8_t tags[] = {100, 103, 104, 105, 106, 107};
    for (const char* p : paths) {
        for (uint8_t t : tags) {
            for (int flags = 0; flags < 8; ++flags) {
                std::vector<uint8_t> data = anmHeader();
                putCurve(data, p, t, 1);
                data.push_back(0);
                char label[128];
                std::snprintf(label, sizeof(label), "curve[%s|%u|%d%d%d]", p, t, flags & 1, (flags >> 1) & 1, (flags >> 2) & 1);
                dumpAnm(data, static_cast<uint8_t>(flags & 1), static_cast<uint8_t>((flags >> 1) & 1),
                    static_cast<uint8_t>((flags >> 2) & 1), label);
            }
        }
    }
    // Truncated prefixes are excluded here: the original reads past the buffer
    // (accepts some as empty, crashes on others), so they are a known divergence.
    // Negative key count, bad magic.
    std::vector<uint8_t> negative = anmHeader();
    negative.push_back(100);
    putWord(negative, 0xffffffffu);
    negative.push_back(0);
    std::vector<uint8_t> wrong;
    putString(wrong, "CM3D2_MENU");
    putWord(wrong, 1);
    wrong.push_back(0);
    dumpAnm(wrong, 0, 0, 0, "wrong-magic");
    std::printf("DONE\n");
}

// ---- CSV ---------------------------------------------------------------------
void batteryCsv(const char* nei) {
    wchar_t wide[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, nei, -1, wide, MAX_PATH);
    auto file_create = fn<void(__cdecl*)(VPP)>("DLL_Win32File_Create");
    auto file_open = fn<uint8_t(__cdecl*)(VPP, const wchar_t*)>("DLL_Win32File_Open");
    auto file_delete = fn<void(__cdecl*)(VPP)>("DLL_Win32File_Delete");
    auto csv_create = fn<void*(__cdecl*)()>("DLL_CSV_CreateCsvParser");
    auto csv_open = fn<void(__cdecl*)(void*, void*)>("DLL_CSV_Open");
    auto csv_delete = fn<void(__cdecl*)(void*)>("DLL_CSV_DeleteCsvParser");
    auto is_valid = fn<uint8_t(__cdecl*)(void*)>("DLL_CSV_IsValid");
    auto max_x = fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellX");
    auto max_y = fn<int32_t(__cdecl*)(void*)>("DLL_CSV_GetMaxCellY");
    auto size_s = fn<int32_t(__cdecl*)(void*, int32_t, int32_t)>("DLL_CSV_GetDataSizeString");
    auto cell_s = fn<void(__cdecl*)(void*, int32_t, int32_t, char*, int32_t)>("DLL_CSV_GetCellAsString");

    void* file = nullptr;
    file_create(&file);
    if (!file_open(&file, wide)) {
        std::printf("open failed\n");
        return;
    }
    void* csv = csv_create();
    csv_open(csv, file);
    std::printf("valid=%d maxX=%d maxY=%d\n", is_valid(csv), max_x(csv), max_y(csv));
    for (int32_t y = 0; y < max_y(csv); ++y) {
        for (int32_t x = 0; x < max_x(csv); ++x) {
            const int32_t size = size_s(csv, x, y);
            std::printf("%d %d %d ", x, y, size);
            if (size > 0) {
                std::vector<char> buffer(static_cast<size_t>(size) + 1, 0);
                cell_s(csv, x, y, buffer.data(), static_cast<int32_t>(buffer.size()));
                for (int32_t i = 0; i < size - 1; ++i) std::printf("%02x", static_cast<unsigned char>(buffer[i]));
            }
            std::printf("\n");
        }
    }
    csv_delete(csv);
    file_delete(&file);
    std::printf("DONE\n");
}

// ---- FileSystemDelegate ------------------------------------------------------
const wchar_t* g_last = nullptr;

void* g_file = nullptr;

uint8_t exists(const wchar_t* path) { g_last = path; return 1; }
uint8_t existsNo(const wchar_t*) { return 0; }
void* getFile(const wchar_t* path) { g_last = path; return g_file; }

void batteryFsd() {
    auto create = fn<void(__cdecl*)(VPP)>("DLL_FileSystemDelegate_Create");
    auto del = fn<void(__cdecl*)(VPP)>("DLL_FileSystemDelegate_Delete");
    auto wide_ptr = fn<void*(__cdecl*)(VPP)>("DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer");
    auto exists_of = fn<uint8_t(__cdecl*)(VPP, const wchar_t*)>("DLL_FileSystemDelegate_IsExistentFile");
    auto reg_exists = fn<void(__cdecl*)(VPP, uint8_t(__cdecl*)(const wchar_t*))>("DLL_FileSystemDelegate_RegisterIsExistentFileCallBack");
    auto reg_get = fn<void(__cdecl*)(VPP, void* (__cdecl*)(const wchar_t*))>("DLL_FileSystemDelegate_RegisterGetFileWideCallBack");
    auto get_file = fn<uint8_t(__cdecl*)(VPP, const wchar_t*, void**)>("DDLL_FileSystemDelegate_GetFile");
    auto mem_create = fn<void(__cdecl*)(VPP)>("DLL_FileMemory_CreateileMemory");
    auto mem_iface = fn<void*(__cdecl*)(VPP)>("DLL_FileMemory_GetInterfaceFilePointer");
    auto mem_set = fn<void(__cdecl*)(VPP, const void*, int32_t)>("DLL_FileMemory_SetData");
    auto mem_del = fn<void(__cdecl*)(VPP)>("DLL_FileMemory_DeleteileMemory");
    auto file_valid = fn<uint8_t(__cdecl*)(VPP)>("DLL_File_IsValid");

    void* d = nullptr;
    create(&d);
    std::printf("wide==obj %d\n", wide_ptr(&d) == d);
    std::printf("exists unregistered=%d\n", exists_of(&d, L"a.txt"));
    void* out = reinterpret_cast<void*>(uintptr_t{1});
    std::printf("getfile unregistered=%d out=%p\n", get_file(&d, L"a.txt", &out), out);
    reg_exists(&d, exists);
    g_last = nullptr;
    std::printf("exists registered=%d path=%d\n", exists_of(&d, L"a.txt"), g_last != nullptr && std::wcscmp(g_last, L"a.txt") == 0);
    g_last = nullptr;
    std::printf("exists null-path=%d called=%d\n", exists_of(&d, nullptr), g_last != nullptr);
    reg_exists(&d, existsNo);
    std::printf("exists rejecting=%d\n", exists_of(&d, L"a.txt"));
    reg_exists(&d, nullptr);
    std::printf("exists cleared=%d\n", exists_of(&d, L"a.txt"));

    reg_get(&d, getFile);
    void* mem = nullptr;
    mem_create(&mem);
    const char bytes[] = "xyz";
    mem_set(&mem, bytes, 3);
    g_file = mem_iface(&mem);
    out = reinterpret_cast<void*>(uintptr_t{1});
    const uint8_t got = get_file(&d, L"a.txt", &out);
    std::printf("getfile valid=%d matches=%d\n", got, out == g_file);
    if (got) mem_del(&out);

    void* empty = nullptr;
    mem_create(&empty);
    std::printf("empty valid=%d\n", file_valid(&empty));
    g_file = empty;
    out = reinterpret_cast<void*>(uintptr_t{1});
    std::printf("getfile invalid=%d out=%p\n", get_file(&d, L"b.txt", &out), out);

    g_last = nullptr;
    std::printf("getfile null-path=%d called=%d\n", get_file(&d, nullptr, &out), g_last != nullptr);
    del(&d);
    std::printf("deleted=%d\n", d == nullptr);
    std::printf("DONE\n");
}

void batteryFsdValid() {
    auto create = fn<void(__cdecl*)(VPP)>("DLL_FileSystemDelegate_Create");
    auto is_valid = fn<uint8_t(__cdecl*)(VPP)>("DLL_FileSystemDelegate_IsValid");
    void* d = nullptr;
    create(&d);
    std::printf("IsValid=%d\n", is_valid(&d));
    std::printf("DONE\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_module = LoadLibraryA(argv[1]);
    if (g_module == nullptr) { std::printf("LOAD FAILED %lu\n", GetLastError()); return 1; }
    const std::string battery = argv[2];
    if (battery == "tjs") batteryTjs();
    else if (battery == "enum") batteryEnum();
    else if (battery == "anm") batteryAnm();
    else if (battery == "csv" && argc >= 4) batteryCsv(argv[3]);
    else if (battery == "fsd") batteryFsd();
    else if (battery == "fsdvalid") batteryFsdValid();
    else return 2;
    return 0;
}
