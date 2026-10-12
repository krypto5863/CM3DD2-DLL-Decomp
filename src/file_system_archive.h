#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "warc_archive.h"
#include "file_system_job_state.h"

namespace cm3d2::internal {

// FileSystemArchive (constructor FUN_180089c40). Each mounted archive carries two table vectors
// (tag 0 with UTF-16 keys and tag 1 with UTF-8 keys). A lookup walks the archives newest first and
// each archive's tables from the last to the first; the root table is the first entry and every
// auto path adds the table found at that folder path.
class ArchiveFolderSet {
public:
    struct Hit {
        const WarcArchive* archive = nullptr;
        std::uint64_t value = 0;
    };

    // FUN_180089f50: mounts an opened archive. The id is the hash of its normalized path; an
    // archive that is already mounted is left alone and the call still succeeds.
    bool addArchive(const std::wstring& normalized_path);

    // FileSystemArchive slot 0x70 (FUN_18008c3e0) with a read at offset 0: copies up to `size` bytes from the start of the
    // mounted archive file whose id is `id`. False when no such archive is mounted or the file cannot be read.
    bool readMountedHead(std::uint64_t id, unsigned char* out, std::size_t size) const;

    // FUN_18008bf90: registers a folder path; every archive searches that folder first.
    bool addAutoPath(std::wstring name);

    // Internal equivalent of the original virtual-base flag setter; no new C ABI.
    void setTreeMode(bool enabled) { tree_mode_ = enabled; }
    bool treeMode() const { return tree_mode_; }
    void addAutoPathForAllFolder(bool asynchronous);
    bool isFinishedAddAutoPathJob(bool wait) const;
    void releaseAddAutoPathJob();

    // FUN_1800895b0: at least one archive is mounted.
    bool valid() const { return !archives_.empty(); }

    // FUN_18008db40 / FUN_18008e420 (narrow names, tag 1 keys) and FUN_18008d6e0 / FUN_18008dfc0
    // (wide names, tag 0 keys). `for_get_file` selects the test on the stored value (non-negative
    // for GetFile, anything but all ones for IsExistentFile).
    bool find(const std::wstring& name, bool for_get_file, Hit& hit, bool wide_keys = false) const;

    // The narrow slots' lookup after FUN_18008e9d0 has turned the UTF-8 name into component hashes (tag 1 tables, tag 1 keys):
    // `keys` is exactly narrow_abi::archiveComponentHashes(name). Empty keys never match. Same precedence and acceptance as find().
    bool findHashes(const std::vector<std::uint64_t>& keys, bool for_get_file, Hit& hit) const;
    // The wide slots' key vector (tag 0 tables, UTF-16 keys) for completeness; same core as find(..., wide_keys = true).
    bool findWideHashes(const std::vector<std::uint64_t>& keys, bool for_get_file, Hit& hit) const;

    // The menu-name set (+0xc8): names with the extension "menu" from every mounted archive.
    std::vector<std::wstring> menuFiles() const { return menu_files_.values(); }

    // FUN_18008c410: sorted, unique names below a folder; mode 0/1 list folders, 2/3 list files,
    // modes 1 and 3 recurse.
    std::vector<std::wstring> list(const std::wstring& name, int mode) const;

    // FUN_18008dfa0: the name map of the last mounted archive in iteration order.
    std::vector<std::wstring> nameMapLastArchive() const;
    const MsvcOrderedNameMap* rawFileNames() const {
        return archives_.empty() ? nullptr : &archives_.back().archive->nameMap();
    }
    const MsvcOrderedStringSet& rawMenuNames() const { return menu_files_; }

private:
    struct Entry {
        std::shared_ptr<const WarcArchive> archive;
        std::uint64_t id = 0;
        std::wstring path;  // normalized path the archive was mounted from
        std::vector<std::size_t> tables[2];
    };
    struct AutoPath {
        std::vector<std::uint64_t> keys[2];
        std::vector<std::uint64_t> processed;
    };

    bool findKeys(const std::vector<std::uint64_t>& keys, int which, bool for_get_file, Hit& hit) const;  // core of find*/FUN_18008db40..
    void applyAutoPaths();  // FUN_18008fb60
    static AutoPath makeAutoPath(std::wstring name);
    void attachAutoPathRange(std::int32_t begin, std::int32_t end);
    std::wstring nameOf(std::uint64_t key) const;  // FUN_18008e880
    void listTable(const WarcArchive& archive, std::size_t table, const std::wstring& prefix, int mode,
        std::vector<std::wstring>& out) const;

    std::vector<Entry> archives_;
    std::vector<AutoPath> auto_paths_;
    MsvcOrderedStringSet menu_files_;
    std::atomic<bool> tree_mode_{false}; // native +0xc0; atomic polling substitution
    AutoPathJobCompletion job_completion_;
    // 18008f600 creates one mutex per capacity slot; release retains capacity.
    std::vector<std::unique_ptr<AutoPathRootMutex>> job_roots_;
};

} // namespace cm3d2::internal
