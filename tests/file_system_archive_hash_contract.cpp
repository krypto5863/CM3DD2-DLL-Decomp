// ArchiveFolderSet::findHashes / findWideHashes against the existing name-based find(), on generated valid archives only.
#include "file_system_archive.h"
#include "file_system_narrow_abi.h"
#include "file_system_windows.h"
#include "support/synthetic_warc.h"

#include <cstdio>

namespace {

using cm3d2::internal::ArchiveFolderSet;
using cm3d2::internal::narrow_abi::archiveComponentHashes;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string readHit(const ArchiveFolderSet::Hit& hit) {
    std::vector<unsigned char> bytes;
    require(hit.archive->read(hit.value, bytes), "plain fixture decode");
    return std::string(bytes.begin(), bytes.end());
}

// Narrow (UTF-8) lookup by name through the hash vector, like the narrow slots after FUN_18008e9d0.
std::string narrow(const ArchiveFolderSet& fs, const char* name, bool for_get_file = true) {
    ArchiveFolderSet::Hit hit;
    if (!fs.findHashes(archiveComponentHashes(name), for_get_file, hit)) return "<missing>";
    return readHit(hit);
}
// The same name through the existing wide-name entry point (tag 1 keys), for equivalence checks.
std::string byName(const ArchiveFolderSet& fs, const wchar_t* name, bool wide_keys = false) {
    ArchiveFolderSet::Hit hit;
    if (!fs.find(name, true, hit, wide_keys)) return "<missing>";
    return readHit(hit);
}
// Wide key vector built independently from the fixture helper (UTF-16 bytes, no lower-casing).
std::string wideKeys(const ArchiveFolderSet& fs, std::initializer_list<const char*> parts) {
    std::vector<std::uint64_t> keys;
    for (const char* part : parts) keys.push_back(synthetic_warc::key(part, false));
    ArchiveFolderSet::Hit hit;
    if (!fs.findWideHashes(keys, true, hit)) return "<missing>";
    return readHit(hit);
}

} // namespace

int main() {
    std::filesystem::path owned;
    try {
        for (int i = 0; i < 100; ++i) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("cm3d2-archive-hash-contract-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { owned = candidate; break; }
        }
        require(!owned.empty(), "fresh owned fixture directory");
        synthetic_warc::fixtures(owned);
        auto mount = [&](ArchiveFolderSet& fs, const char* name) {
            require(fs.addArchive(cm3d2::internal::normalizeFileSystemName((owned / name).wstring())), "mount plain archive");
        };

        {
            ArchiveFolderSet fs;
            ArchiveFolderSet::Hit hit;
            require(!fs.findHashes(archiveComponentHashes("same.txt"), true, hit), "no archive mounted");
            mount(fs, "a.arc");
            mount(fs, "b.arc");
            mount(fs, "c.arc");

            // Empty key vector (null / empty-component handling) never matches and never reaches a table.
            require(!fs.findHashes({}, true, hit) && !fs.findHashes(archiveComponentHashes(nullptr), false, hit), "empty key vector");

            // Newest archive first; root before nested; qualified names descend relative to the table.
            require(narrow(fs, "same.txt") == "B-root", "newest archive wins a bare root name");
            require(narrow(fs, "only-a.txt") == "A-only" && narrow(fs, "only-c.txt") == "C-only", "older archives are still searched");
            require(narrow(fs, "sub\\same.txt") == "B-sub", "qualified name, newest archive");
            require(narrow(fs, "sub/same.txt") == "B-sub", "forward slash");
            require(narrow(fs, "sub\\\\same.txt") == "B-sub", "interior empty component skipped by the normaliser");
            require(narrow(fs, "other\\choose.txt") == "B-other-choice", "other directory");
            require(narrow(fs, "sub\\inner\\choose.txt") == "A-inner-choice", "two-level descent falls back to the archive that has it");
            require(narrow(fs, "sub\\inner\\") == "<missing>" && narrow(fs, "sub\\") == "<missing>", "a directory name is not a file");
            require(narrow(fs, "same.txt \\") == "B-root" && narrow(fs, "sub\\same.txt/") == "B-sub", "trailing spaces and separators are trimmed before hashing");

            // Recursive bare-name fallback (default mode) finds names that exist only below the root.
            require(narrow(fs, "choose.txt") != "<missing>", "bare name found through the recursive fallback in default mode");
            require(narrow(fs, "c.ks") == "C-sub\n", "recursive fallback reaches a nested file of the newest archive");

            // Case folding is the narrow normaliser's job (ASCII).
            require(narrow(fs, "SUB\\SAME.TXT") == "B-sub" && narrow(fs, "Same.Txt") == "B-root", "ASCII case folded before hashing");

            // Equivalent to the name-based entry point for every name above (same hit, not just the same text).
            struct Pair { const wchar_t* wide; const char* narrow; };
            for (const Pair& name : {Pair{L"same.txt", "same.txt"}, Pair{L"only-a.txt", "only-a.txt"}, Pair{L"sub\\same.txt", "sub\\same.txt"},
                                     Pair{L"sub\\inner\\choose.txt", "sub\\inner\\choose.txt"}, Pair{L"other\\choose.txt", "other\\choose.txt"},
                                     Pair{L"choose.txt", "choose.txt"}, Pair{L"c.ks", "c.ks"}}) {
                ArchiveFolderSet::Hit by_name, by_hash;
                require(fs.find(name.wide, true, by_name) && fs.findHashes(archiveComponentHashes(name.narrow), true, by_hash), "both lookups succeed");
                require(by_name.archive == by_hash.archive && by_name.value == by_hash.value, "findHashes == find on the same name");
            }

            // exists vs open acceptance share the table walk.
            require(narrow(fs, "same.txt", false) == "B-root" && narrow(fs, "nope.txt", false) == "<missing>", "exists-style acceptance");

            // Wide versus narrow key selection: non-ASCII names hash differently in tag 0 (UTF-16) and tag 1 (UTF-8) tables.
            require(narrow(fs, u8"日本.txt") == "A-unicode", "UTF-8 key finds the non-ASCII name in the tag 1 table");
            require(wideKeys(fs, {u8"日本.txt"}) == "A-unicode", "UTF-16 key finds it in the tag 0 table");
            {
                ArchiveFolderSet::Hit wrong;
                std::vector<std::uint64_t> utf8_hashes = archiveComponentHashes(u8"日本.txt");
                require(!fs.findWideHashes(utf8_hashes, true, wrong), "UTF-8 hashes do not match the tag 0 table");
                std::vector<std::uint64_t> utf16_hashes{synthetic_warc::key(u8"日本.txt", false)};
                require(!fs.findHashes(utf16_hashes, true, wrong), "UTF-16 hashes do not match the tag 1 table");
            }
            require(wideKeys(fs, {"sub", "same.txt"}) == "B-sub", "wide key vector, qualified");
            require(byName(fs, L"same.txt", true) == "B-root", "wide name lookup unchanged");

            // Auto-path precedence: a registered folder is searched before the root, newest table first.
            require(fs.addAutoPath(L"sub"), "register auto path");
            require(narrow(fs, "same.txt") == "B-sub", "auto-path table precedes the root table");
            require(narrow(fs, "choose.txt") == "B-sub-choice", "bare name resolved inside the auto-path folder");
            require(wideKeys(fs, {"same.txt"}) == "B-sub", "auto-path applies to the tag 0 tables as well");
            require(narrow(fs, "only-a.txt") == "A-only", "root table still searched after the auto path");

            // Enabled (tree) mode: a bare name is no longer searched recursively below a table.
            require(narrow(fs, "inner.ks") == "A-inner\n", "default mode: bare name found by recursion below the auto-path table");
            fs.setTreeMode(true);
            require(narrow(fs, "inner.ks") == "<missing>", "tree mode disables the recursive bare-name fallback for hashes too");
            require(narrow(fs, "sub\\inner\\inner.ks") == "A-inner\n", "qualified lookup is unaffected");
            require(narrow(fs, "c.ks") == "C-sub\n", "a name directly inside the auto-path table is still found");
            fs.setTreeMode(false);
        }

        {   // mount order decides precedence
            ArchiveFolderSet fs;
            mount(fs, "b.arc");
            mount(fs, "a.arc");
            require(narrow(fs, "same.txt") == "A-root", "the later mount is the newer archive");
        }
        std::error_code ignored;
        std::filesystem::remove_all(owned, ignored);
        std::puts("file_system_archive_hash_contract: ok");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        if (!owned.empty()) { std::error_code ignored; std::filesystem::remove_all(owned, ignored); }
        return 1;
    }
}
