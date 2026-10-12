// Usage: fs_archive_battery <dll> [ab|ba|pre]. Creates a NEW owned temp directory
// and only mounts its synthetic plain WARCs. No caller-supplied archives are accepted.
// Wide opens are exercised through the DLL's KAG/Menu consumers because the
// reconstruction currently exposes interface tokens, not callable native vtables.
#include "../../tests/support/synthetic_warc.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

struct Fs { void* object; std::int32_t kind; std::int32_t reserved; };
struct List { std::int32_t count; std::int32_t reserved; void* data; };
static HMODULE module;
template<class T> T fn(const char* name) {
    const auto f = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!f) throw std::runtime_error(std::string("missing export: ") + name);
    return f;
}
using FsString = std::uint8_t(__cdecl*)(Fs*, const char*);
static void dump(const char* title, List& list) {
    std::printf("%s count=%d\n", title, list.count);
    for (int i = 0; i < list.count; ++i) {
        char b[512]{};
        fn<std::uint32_t(__cdecl*)(List*, int, char*, int)>("DLL_FileSystem_AtList")(&list, i, b, sizeof(b));
        std::printf("  [%d] %s\n", i, b);
    }
    fn<void(__cdecl*)(List*)>("DLL_FileSystem_DeleteList")(&list);
}
static void look(Fs& fs, const char* name) {
    void* file = nullptr;
    const auto exists = fn<FsString>("DLL_FileSystem_IsExistentFile")(&fs, name);
    const auto got = fn<std::uint8_t(__cdecl*)(Fs*, const char*, void**)>("DLL_FileSystem_GetFile")(&fs, name, &file);
    std::uint64_t size = 0;
    std::string text = "<none>";
    if (got) {
        size = fn<std::uint64_t(__cdecl*)(void**)>("DLL_File_GetSize")(&file);
        char b[128]{};
        const auto n = fn<std::uint64_t(__cdecl*)(void**, void*, std::uint64_t)>("DLL_File_Read")(&file, b, sizeof(b));
        if (n > sizeof(b)) throw std::runtime_error("invalid read length");
        text.assign(b, static_cast<std::size_t>(n));
        fn<void(__cdecl*)(void**)>("DLL_File_CloseFile")(&file);
    }
    std::printf("lookup '%s': exists=%u get=%u size=%llu content=%s\n", name, exists, got,
                static_cast<unsigned long long>(size), text.c_str());
}
static void lookups(Fs& fs) {
    for (const char* n : {"same.txt", "SAME.TXT", "same.txt ", "only-a.txt", "only-b.txt", "only-c.txt",
        "choose.txt", "sub/same.txt", "sub\\same.txt", "SUB/INNER/choose.txt", "other/choose.txt",
        "inner/choose.txt", "sub", "sub/", "", "missing.txt", "noext", u8"\u65e5\u672c.txt"}) look(fs, n);
}
static void wideConsumers(Fs& fs) {
    void* wide = fn<void*(__cdecl*)(Fs*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&fs);
    for (const char* name : {"same.ks", "sub/same.ks", "inner.ks", "sub/inner/inner.ks", "other.ks", u8"\u65e5\u672c.ks"}) {
        // A fresh interpreter + cleared scenario cache prevents an earlier open
        // hiding archive/auto-path precedence changes in this battery.
        fn<void(__cdecl*)(void**)>("DLL_KAG_ClearScnearioCache")(nullptr);
        void* tjs = nullptr; void* kag = nullptr;
        fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &tjs);
        fn<void(__cdecl*)(void**, void**)>("DLL_KAG_Create")(&tjs, &kag);
        fn<void(__cdecl*)(void**, void*)>("DLL_KAG_SetFileSystem")(&kag, wide);
        fn<void(__cdecl*)(void**, const char*)>("DLL_KAG_LoadScenario")(&kag, name);
        for (int step = 0; step < 8 && fn<std::uint8_t(__cdecl*)(void**)>("DLL_KAG_IsExecScenario")(&kag); ++step)
            fn<int(__cdecl*)(void**)>("DLL_KAG_Exec")(&kag);
        char b[512]{};
        fn<void(__cdecl*)(void**, char*, int)>("DLL_KAG_GetText")(&kag, b, sizeof(b));
        for (char& c : b) if (c == '\n' || c == '\r') c = '|';
        std::printf("wide-KAG '%s': text=%s\n", name, b);
        fn<void(__cdecl*)(void**)>("DLL_KAG_Delete")(&kag);
        fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&tjs);
    }
    void* cats = fn<void*(__cdecl*)()>("DLL_EnumData_CreateEnumData")();
    void* colors = fn<void*(__cdecl*)()>("DLL_EnumData_CreateEnumData")();
    fn<std::uint8_t(__cdecl*)(void*, int, const char*)>("DLL_EnumData_AddMember")(cats, 1, "wear");
    void* database = nullptr;
    fn<void(__cdecl*)(void**, void*, void*)>("DLL_MenuDataBase_New")(&database, cats, colors);
    fn<void(__cdecl*)(void**, void*)>("DLL_MenuDataBase_StartAnalysis")(&database, wide);
    const auto done = fn<std::uint8_t(__cdecl*)(void**)>("DLL_MenuDataBase_JobFinished");
    for (int i = 0; i < 500 && !done(&database); ++i) Sleep(10);
    if (!done(&database)) throw std::runtime_error("menu worker timeout");
    const auto count = fn<std::int64_t(__cdecl*)(void**)>("DLL_MenuDataBase_Size")(&database);
    std::printf("wide-menu count=%lld\n", static_cast<long long>(count));
    for (int i = 0; i < count; ++i) {
        fn<void(__cdecl*)(void**, int)>("DLL_MenuDataBase_SetIndex")(&database, i);
        const auto hash = fn<std::uint64_t(__cdecl*)(void**)>("DLL_MenuParse_GetNativeHash")(&database);
        const char* name = fn<const char*(__cdecl*)(void**, std::uint64_t)>("DLL_MenuDataBase_GetFileNameToNativeHash")(&database, hash);
        const char* title = fn<const char*(__cdecl*)(void**)>("DLL_MenuParse_GetMenuName")(&database);
        std::printf("  menu %s: %s\n", name ? name : "<null>", title ? title : "<null>");
    }
    fn<void(__cdecl*)(void**)>("DLL_MenuDataBase_Delete")(&database);
    fn<void(__cdecl*)(void*)>("DLL_EnumData_DeleteEnumData")(cats);
    fn<void(__cdecl*)(void*)>("DLL_EnumData_DeleteEnumData")(colors);
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Limits this probe process only, including a native call that never returns.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        std::fputs("battery watchdog timeout\n", stderr);
        ExitProcess(124);
    }).detach();
    std::filesystem::path owned;
    try {
        if (argc < 2 || argc > 3) return 2;
        const std::string order = argc == 3 ? argv[2] : "ab";
        if (order != "ab" && order != "ba" && order != "pre") return 2;
        module = LoadLibraryW(synthetic_warc::wide(argv[1]).c_str());
        if (!module) throw std::runtime_error("LoadLibrary failed");
        // Create a fresh directory atomically; never delete/reuse caller paths.
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("cm3d2-archive-battery-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) { owned = candidate; break; }
        }
        if (owned.empty()) throw std::runtime_error("cannot allocate fixture directory");
        synthetic_warc::fixtures(owned);
        Fs fs{};
        fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemArchive")(&fs);
        const auto add = fn<FsString>("DLL_FileSystem_AddArchive");
        const auto autoPath = fn<FsString>("DLL_FileSystem_AddAutoPath");
        auto mount = [&](const char* name) {
            const auto path = (owned / name).u8string();
            std::printf("AddArchive(%s)=%u\n", name, add(&fs, path.c_str()));
        };
        std::printf("case=%s fresh-valid=%u\n", order.c_str(), fn<std::uint8_t(__cdecl*)(Fs*)>("DLL_FileSystem_IsValid")(&fs));
        mount("missing.arc"); mount("plain.txt");
        if (order == "pre") std::printf("pre-auto(sub)=%u\n", autoPath(&fs, "sub"));
        if (order == "ba") { mount("b.arc"); mount("a.arc"); } else { mount("a.arc"); mount("b.arc"); }
        mount("a.arc");
        std::puts("-- initial"); lookups(fs);
        for (const char* path : {"sub", "SUB/", "sub/inner", "other", "missing", ""})
            std::printf("AddAutoPath(%s)=%u\n", path, autoPath(&fs, path));
        std::puts("-- auto paths"); lookups(fs);
        for (const std::uint8_t threads : {std::uint8_t(0), std::uint8_t(1)}) {
            fn<void(__cdecl*)(Fs*, std::uint8_t)>("DLL_FileSystem_AddAutoPathForAllFolder")(&fs, threads);
            std::printf("all-paths(%u) finished=%u\n", threads,
                fn<std::uint8_t(__cdecl*)(Fs*, std::uint8_t)>("DLL_FileSystem_IsFinishedAddAutoPathJob")(&fs, 0));
            fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_ReleaseAddAutoPathJob")(&fs);
        }
        mount("c.arc");
        std::puts("-- third archive"); lookups(fs);
        wideConsumers(fs);
        for (const char* path : {"", "*", "sub", "sub/", "sub/inner", "other", "missing"}) {
            for (int mode = 0; mode < 4; ++mode) {
                List list{}; char label[128];
                fn<void(__cdecl*)(Fs*, const char*, int, List*)>("DLL_FileSystem_CreateList")(&fs, path, mode, &list);
                std::snprintf(label, sizeof(label), "list('%s',%d)", path, mode); dump(label, list);
            }
        }
        for (const char* extension : {"*", "", "txt", "TXT", ".txt", "ks", "menu"}) {
            List list{}; char label[128];
            fn<void(__cdecl*)(Fs*, const char*, List*)>("DLL_FileSystem_CreateExtensionList")(&fs, extension, &list);
            std::snprintf(label, sizeof(label), "extension('%s')", extension); dump(label, list);
        }
        auto names = [&] {
            for (const wchar_t* filter : {static_cast<const wchar_t*>(nullptr), L"", L"x.txt", L"x.ks", L"x.menu"}) {
                List list{}; char label[128];
                fn<void(__cdecl*)(Fs*, const wchar_t*, List*)>("DLL_FileSystem_GetNameMapLastArchive")(&fs, filter, &list);
                std::snprintf(label, sizeof(label), "last-map(%ls)", filter ? filter : L"null"); dump(label, list);
            }
        };
        names(); mount("empty.arc"); names();
        std::printf("empty-mounted-valid=%u\n", fn<std::uint8_t(__cdecl*)(Fs*)>("DLL_FileSystem_IsValid")(&fs));
        look(fs, "same.txt");
        fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&fs);
        FreeLibrary(module);
        // Exact directory created above by this process, containing only fixtures.
        // Native mount handles can remain open after deletion. Preserve leftovers
        // for host cleanup after process exit; do not turn cleanup into a result diff.
        std::error_code cleanup_error;
        std::filesystem::remove_all(owned, cleanup_error);
        if (cleanup_error) std::wcerr << L"cleanup deferred until process exit: " << owned.wstring() << L'\n';
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "battery failed: %s\n", e.what());
        // Preserve failed fixtures for inspection; report their owned location.
        std::wcerr << L"fixtures retained: " << owned.wstring() << L'\n';
        return 1;
    }
}
