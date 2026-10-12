// Bounded enabled-mode native probe. ONLY creates/mounts a fresh tiny loose tree.
// Usage: fs_job_probe <verified original DLL> <async 0/1> <dirs 0..16> <preset 0/1>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#ifdef FS_JOB_INTERNAL
#include "../../src/file_system_windows.h"
using cm3d2::internal::WindowsFolderSet;
#endif
struct Fs { void* object; int kind; int reserved; };
static HMODULE module;
template<class T> T fn(const char* name) {
    const auto p = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!p) throw std::runtime_error(name);
    return p;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 5) return 2;
    const int async = std::atoi(argv[2]), dirs = std::atoi(argv[3]), preset = std::atoi(argv[4]);
    if ((async != 0 && async != 1) || dirs < 0 || dirs > 16 || (preset != 0 && preset != 1)) return 2;
    std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(5));
        std::puts("watchdog-timeout"); ExitProcess(124); }).detach();
    try {
#ifndef FS_JOB_INTERNAL
        module = LoadLibraryA(argv[1]);
        if (!module) return 3;
#endif
        std::filesystem::path root;
        for (int i = 0; i < 100; ++i) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("cm3d2-job-probe-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) {root=candidate; break;}
        }
        if (root.empty()) return 3;
        for (int i = 0; i < dirs; ++i) {
            const auto sub = root / ("d" + std::to_string(i));
            std::filesystem::create_directory(sub);
            std::ofstream(sub / "one.txt") << "fixture";
        }
        Fs fs{};
#ifdef FS_JOB_INTERNAL
        auto set = std::make_unique<WindowsFolderSet>();
        set->setTreeMode(true);
        std::printf("async=%d dirs=%d preset=%d add=%u\n", async, dirs, preset, set->addFolder(root.wstring(), L""));
        if (preset && dirs) set->addAutoPath(L"d0");
        std::puts("before-all");
        set->addAutoPathForAllFolder(async != 0);
        std::puts("after-all");
        std::printf("poll=%u\n", set->isFinishedAddAutoPathJob(false));
        if (async) std::printf("wait=%u\n", set->isFinishedAddAutoPathJob(true));
        set->releaseAddAutoPathJob();
        std::puts("released");
        set.reset();
#else
        fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemWindows")(&fs);
        // Verified Windows original vbtable places virtual base at +0x1e0.
        char* base = static_cast<char*>(fs.object) + 0x1e0;
        void** vt = *reinterpret_cast<void***>(base);
        reinterpret_cast<unsigned char(__cdecl*)(void*, unsigned char)>(vt[3])(base, 1);
        std::printf("async=%d dirs=%d preset=%d add=%u\n", async, dirs, preset,
            fn<unsigned char(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddFolder")(&fs, root.u8string().c_str()));
        if (preset && dirs) fn<unsigned char(__cdecl*)(Fs*, const char*)>("DLL_FileSystem_AddAutoPath")(&fs, "d0");
        std::puts("before-all");
        fn<void(__cdecl*)(Fs*, unsigned char)>("DLL_FileSystem_AddAutoPathForAllFolder")(&fs, static_cast<unsigned char>(async));
        std::puts("after-all");
        const auto done = fn<unsigned char(__cdecl*)(Fs*, unsigned char)>("DLL_FileSystem_IsFinishedAddAutoPathJob");
        std::printf("poll=%u\n", done(&fs, 0));
        if (async) std::printf("wait=%u\n", done(&fs, 1));
        fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_ReleaseAddAutoPathJob")(&fs);
        std::puts("released");
        fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&fs);
        FreeLibrary(module);
#endif
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
