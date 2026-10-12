// Usage: fs_archive_job_probe <original.dll.orig|internal> <async 0/1> <preset 0/1>
// Only freshly generated plain WARCs are accepted; no caller archive paths.
#include "../../tests/support/synthetic_warc.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#ifdef ARCHIVE_JOB_INTERNAL
#include "../../src/file_system_archive.h"
#include "../../src/file_system_windows.h"
#endif

struct Fs { void* object; int kind; int reserved; };
static HMODULE module;
template<class T> T fn(const char* name) {
    const auto p = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!p) throw std::runtime_error(name);
    return p;
}
struct Backend {
#ifdef ARCHIVE_JOB_INTERNAL
    cm3d2::internal::ArchiveFolderSet fs;
    void create() { fs.setTreeMode(true); }
    bool add(const std::filesystem::path& path) {
        return fs.addArchive(cm3d2::internal::normalizeFileSystemName(path.wstring()));
    }
    bool autoPath(const char* path) { return fs.addAutoPath(synthetic_warc::wide(path)); }
    void all(bool async) { fs.addAutoPathForAllFolder(async); }
    bool done(bool wait) { return fs.isFinishedAddAutoPathJob(wait); }
    void release() { fs.releaseAddAutoPathJob(); }
    void destroy() {}
    void lookup(const char* name) {
        cm3d2::internal::ArchiveFolderSet::Hit hit;
        const bool exists = fs.find(synthetic_warc::wide(name), false, hit);
        const bool got = fs.find(synthetic_warc::wide(name), true, hit);
        std::string text = "<none>";
        if (got) { std::vector<unsigned char> data; if (!hit.archive->read(hit.value, data)) throw std::runtime_error("fixture decode"); text.assign(data.begin(), data.end()); }
        std::printf("lookup '%s': e=%u g=%u text=%s\n", name, exists, got, text.c_str());
    }
#else
    Fs fs{};
    void create() {
        fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemArchive")(&fs);
        // Original archive virtual base at +0x110; getter/setter at slots 2/3.
        auto* base = static_cast<char*>(fs.object) + 0x110;
        auto** vt = *reinterpret_cast<void***>(base);
        reinterpret_cast<unsigned char(__cdecl*)(void*, unsigned char)>(vt[3])(base, 1);
    }
    bool add(const std::filesystem::path& path) { return fn<unsigned char(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddArchive")(&fs, path.u8string().c_str()); }
    bool autoPath(const char* path) { return fn<unsigned char(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddAutoPath")(&fs, path); }
    void all(bool async) { fn<void(__cdecl*)(Fs*, unsigned char)>("DLL_FileSystem_AddAutoPathForAllFolder")(&fs, async); }
    bool done(bool wait) { return fn<unsigned char(__cdecl*)(Fs*, unsigned char)>("DLL_FileSystem_IsFinishedAddAutoPathJob")(&fs, wait); }
    void release() { fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_ReleaseAddAutoPathJob")(&fs); }
    void destroy() { fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&fs); }
    void lookup(const char* name) {
        const auto exists = fn<unsigned char(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_IsExistentFile")(&fs, name);
        void* file = nullptr;
        const auto got = fn<unsigned char(__cdecl*)(Fs*, const char*, void**)>("DLL_FileSystem_GetFile")(&fs, name, &file);
        std::string text = "<none>";
        if (got) {
            char b[128]{};
            const auto n = fn<std::uint64_t(__cdecl*)(void**, void*, std::uint64_t)>("DLL_File_Read")(&file, b, sizeof(b));
            if (n > sizeof(b)) throw std::runtime_error("fixture read bound");
            text.assign(b, static_cast<std::size_t>(n));
            fn<void(__cdecl*)(void**)>("DLL_File_CloseFile")(&file);
        }
        std::printf("lookup '%s': e=%u g=%u text=%s\n", name, exists, got, text.c_str());
    }
#endif
};
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 4) return 2;
    const int async = std::atoi(argv[2]), preset = std::atoi(argv[3]);
    if ((async != 0 && async != 1) || (preset != 0 && preset != 1)) return 2;
    std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(5));
        std::puts("watchdog-timeout"); ExitProcess(124); }).detach();
    try {
#ifndef ARCHIVE_JOB_INTERNAL
        module = LoadLibraryW(synthetic_warc::wide(argv[1]).c_str());
        if (!module) return 3;
#endif
        std::filesystem::path owned;
        for (int i=0; i<100; ++i) {
            const auto p = std::filesystem::temp_directory_path() /
                ("cm3d2-archive-job-probe-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(p)) { owned=p; break; }
        }
        if (owned.empty()) return 3;
        using namespace synthetic_warc;
        std::vector<Member> members{{"same.txt", text("A-root")}};
        for (int i=0; i<9; ++i) members.push_back({"d"+std::to_string(i)+"/f"+std::to_string(i)+".txt", text("A-"+std::to_string(i))});
        members.push_back({"d0/same.txt", text("A-d0")});
        members.push_back({"d0/inner/deep.txt", text("A-deep")});
        members.push_back({u8"\u65e5\u672c/uni.txt", text("A-unicode")});
        write(owned/"a.arc", archive(members));
        write(owned/"b.arc", archive({{"same.txt", text("B-root")}, {"d0/same.txt", text("B-d0")}, {"d8/b.txt", text("B-d8")}}));
        write(owned/"c.arc", archive({{"late/new.txt", text("C-late")}}));
        {
            Backend fs; fs.create();
            std::printf("async=%d preset=%d initial-poll=%u\n", async, preset, fs.done(false));
            std::printf("add-a=%u\n", fs.add(owned/"a.arc"));
            std::printf("add-b=%u\n", fs.add(owned/"b.arc"));
            if (preset) std::printf("pre-auto-d0=%u\n", fs.autoPath("d0"));
            fs.lookup("f0.txt"); fs.lookup("same.txt");
            fs.all(async != 0); std::puts("all-returned");
            if (async) std::printf("wait=%u\n", fs.done(true));
            std::printf("settled-poll=%u\n", fs.done(false));
            for (const char* name : {"f0.txt", "f8.txt", "same.txt", "deep.txt", "uni.txt", "d0/f0.txt", "b.txt", "missing.txt"}) fs.lookup(name);
            fs.release(); std::printf("released-poll=%u\n", fs.done(false));
            std::printf("add-c=%u\n", fs.add(owned/"c.arc"));
            fs.all(async != 0); std::puts("second-all-returned");
            if (async) std::printf("second-wait=%u\n", fs.done(true));
            fs.lookup("new.txt");
            fs.all(false); fs.lookup("new.txt");
            fs.release(); fs.destroy();
        }
#ifndef ARCHIVE_JOB_INTERNAL
        FreeLibrary(module);
#endif
        std::error_code ignored;
        std::filesystem::remove_all(owned, ignored); // only the fresh owned fixture root
        if (ignored) std::fprintf(stderr, "fixture cleanup deferred: %s\n", owned.u8string().c_str());
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
