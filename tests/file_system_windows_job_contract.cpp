#include "file_system_windows.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int main() {
    std::filesystem::path owned;
    try {
        for (int i = 0; i < 100; ++i) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("cm3d2-windows-job-contract-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { owned = candidate; break; }
        }
        require(!owned.empty(), "fresh fixture root");
        for (int i = 0; i < 9; ++i) {
            const auto directory = owned / "first" / ("d" + std::to_string(i));
            std::filesystem::create_directories(directory);
            std::ofstream(directory / ("f" + std::to_string(i) + ".txt")) << "fixture";
        }
        using cm3d2::internal::WindowsFolderSet;
        {
            WindowsFolderSet fs;
            require(fs.isFinishedAddAutoPathJob(false), "default mode reports finished");
            fs.setTreeMode(true);
            require(fs.addFolder((owned / "first").wstring(), L""), "tree mount");
            require(fs.resolve(L"f0.txt").empty(), "no recursive bare lookup in tree mode");
            require(!fs.isFinishedAddAutoPathJob(false), "enabled mode starts with zero flags");
            fs.addAutoPathForAllFolder(false);
            require(!fs.resolve(L"f0.txt").empty(), "synchronous all-folder attachment");
            require(!fs.isFinishedAddAutoPathJob(false), "sync attachment does not set worker flags");
            fs.releaseAddAutoPathJob();
            fs.setTreeMode(false);
            require(fs.isFinishedAddAutoPathJob(true), "mode clear stops completion wait");
        }
        {
            WindowsFolderSet fs;
            fs.setTreeMode(true);
            require(fs.addFolder((owned / "first").wstring(), L""), "fresh tree mount");
            fs.addAutoPathForAllFolder(true);
            require(fs.isFinishedAddAutoPathJob(true), "eight fresh detached workers complete");
            for (int i = 0; i < 9; ++i)
                require(!fs.resolve(L"f" + std::to_wstring(i) + L".txt").empty(), "each range attached");
            fs.releaseAddAutoPathJob();
            require(fs.isFinishedAddAutoPathJob(false), "release preserves completed flags");
            const auto second = owned / "second" / "newdir";
            std::filesystem::create_directories(second);
            std::ofstream(second / "new.txt") << "new";
            require(fs.addFolder((owned / "second").wstring(), L""), "second root");
            fs.addAutoPathForAllFolder(true);
            require(fs.resolve(L"new.txt").empty(), "completed start skips newly generated paths");
            require(fs.isFinishedAddAutoPathJob(false), "one-shot completion not reset");
            fs.addAutoPathForAllFolder(false);
            require(!fs.resolve(L"new.txt").empty(), "sync path still attaches after completed async job");
        }
        std::filesystem::remove_all(owned); // exact directory created by this process
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1; // preserve failed fixtures
    }
}
