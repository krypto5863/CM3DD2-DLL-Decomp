#include "file_system_archive.h"
#include "file_system_windows.h"
#include "support/synthetic_warc.h"
#include <cstdio>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool exists(const cm3d2::internal::ArchiveFolderSet& fs, const wchar_t* name, bool wide = false) {
    cm3d2::internal::ArchiveFolderSet::Hit hit;
    return fs.find(name, false, hit, wide);
}
std::string read(const cm3d2::internal::ArchiveFolderSet& fs, const wchar_t* name, bool wide = false) {
    cm3d2::internal::ArchiveFolderSet::Hit hit;
    if (!fs.find(name, true, hit, wide)) return "<missing>";
    std::vector<unsigned char> bytes;
    require(hit.archive->read(hit.value, bytes), "plain fixture decode");
    return std::string(bytes.begin(), bytes.end());
}
}

// Finite golden expectations corroborated by fs_archive_job_probe on the
// verified original. All fixtures are generated; no optional game samples.
int main() {
    std::filesystem::path owned;
    try {
        for (int i=0; i<100; ++i) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("cm3d2-archive-job-contract-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { owned=candidate; break; }
        }
        require(!owned.empty(), "fresh owned fixture directory");
        using namespace synthetic_warc;
        std::vector<Member> members{{"same.txt", text("A-root")}};
        for (int i=0; i<9; ++i) members.push_back({"d"+std::to_string(i)+"/f"+std::to_string(i)+".txt", text("A-"+std::to_string(i))});
        members.push_back({"d0/same.txt", text("A-d0")});
        members.push_back({"d0/inner/deep.txt", text("A-deep")});
        members.push_back({u8"\u65e5\u672c/uni.txt", text("A-unicode")});
        write(owned/"a.arc", archive(members));
        write(owned/"b.arc", archive({{"same.txt", text("B-root")}, {"d0/same.txt", text("B-d0")}}));
        write(owned/"c.arc", archive({{"late/new.txt", text("C-late")}}));
        using cm3d2::internal::ArchiveFolderSet;
        auto mount = [&](ArchiveFolderSet& fs, const char* name) {
            require(fs.addArchive(cm3d2::internal::normalizeFileSystemName((owned/name).wstring())), "mount plain archive");
        };
        {
            ArchiveFolderSet fs;
            require(fs.isFinishedAddAutoPathJob(false), "default mode immediately finished");
            mount(fs, "a.arc");
            require(exists(fs, L"f0.txt"), "default mode recursive fallback");
            fs.setTreeMode(true);
            require(!exists(fs, L"f0.txt") && !exists(fs, L"f0.txt", true), "enabled mode disables recursive bare fallback");
            fs.addAutoPathForAllFolder(false);
            require(read(fs, L"deep.txt") == "A-deep", "synchronous nested attachment");
            require(read(fs, L"uni.txt", true) == "A-unicode", "UTF-16 parallel auto-path attachment");
            require(!fs.isFinishedAddAutoPathJob(false), "synchronous work does not set completion flags");
            fs.releaseAddAutoPathJob();
            fs.setTreeMode(false);
            require(fs.isFinishedAddAutoPathJob(true), "disabled wait returns immediately");
        }
        for (const bool preset : {false, true}) {
            ArchiveFolderSet fs;
            fs.setTreeMode(true);
            mount(fs, "a.arc"); mount(fs, "b.arc");
            if (preset) require(fs.addAutoPath(L"d0"), "pre-register path");
            fs.addAutoPathForAllFolder(true);
            require(fs.isFinishedAddAutoPathJob(true), "eight archive workers complete");
            for (int i=0; i<9; ++i)
                require(read(fs, (L"f"+std::to_wstring(i)+L".txt").c_str()) == "A-"+std::to_string(i), "all ranges attached");
            require(read(fs, L"same.txt") == "B-d0", "newest archive/table precedence");
            require(read(fs, L"deep.txt", true) == "A-deep", "wide table nested attachment");
            require(read(fs, L"uni.txt") == "A-unicode" && read(fs, L"uni.txt", true) == "A-unicode", "both key encodings");
            fs.releaseAddAutoPathJob();
            require(fs.isFinishedAddAutoPathJob(false), "release preserves flags");
            mount(fs, "c.arc");
            fs.addAutoPathForAllFolder(true);
            require(fs.isFinishedAddAutoPathJob(true) && !exists(fs, L"new.txt"), "completed start skips newly generated work");
            fs.addAutoPathForAllFolder(false);
            require(read(fs, L"new.txt") == "C-late", "sync applies skipped new work");
        }
        {
            ArchiveFolderSet fs;
            fs.setTreeMode(true);
            fs.addAutoPathForAllFolder(true);
            require(fs.isFinishedAddAutoPathJob(true), "empty ranges still complete all eight flags");
            fs.releaseAddAutoPathJob();
        }
        std::filesystem::remove_all(owned); // exact fresh fixture directory only
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "archive job contract failed: %s\n", e.what());
        return 1; // retain failed fixtures
    }
}
