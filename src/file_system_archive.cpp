#include "file_system_archive.h"

#include "file_system_windows.h"

#include <algorithm>
#include <fstream>
#include <mutex>

namespace cm3d2::internal {

bool ArchiveFolderSet::addArchive(const std::wstring& normalized_path) {
    const std::uint64_t id = fileSystemNameHash(normalized_path);
    WarcArchive archive;
    if (!WarcArchive::open(normalized_path, archive)) {
        return false;
    }
    for (const auto& entry : archives_) {
        if (entry.id == id) {
            return true;
        }
    }
    Entry entry;
    entry.archive = std::make_shared<const WarcArchive>(std::move(archive));
    entry.id = id;
    entry.path = normalized_path;
    entry.tables[0].push_back(0);
    entry.tables[1].push_back(0);
    // FUN_180089f50: every tag 3 name whose extension is exactly "menu" joins the menu set.
    for (const auto& item : entry.archive->names()) {
        const auto dot = item.name.rfind(L'.');
        std::wstring extension = dot == std::wstring::npos ? std::wstring() : item.name.substr(dot + 1);
        while (!extension.empty() && extension.back() == L' ') {
            extension.pop_back();
        }
        if (extension == L"menu") {
            menu_files_.insert(item.name);
        }
    }
    archives_.push_back(std::move(entry));
    applyAutoPaths();
    return true;
}

bool ArchiveFolderSet::readMountedHead(std::uint64_t id, unsigned char* out, std::size_t size) const {
    for (const auto& entry : archives_) {
        if (entry.id != id) {
            continue;
        }
        return entry.archive->readHead(out, size);
    }
    return false;
}

void ArchiveFolderSet::applyAutoPaths() {
    for (auto& path : auto_paths_) {
        for (auto& entry : archives_) {
            if (std::find(path.processed.begin(), path.processed.end(), entry.id) != path.processed.end()) {
                continue;
            }
            path.processed.push_back(entry.id);
            for (int which = 0; which < 2; ++which) {
                if (path.keys[which].empty()) {
                    continue;
                }
                std::size_t table = 0;
                if (entry.archive->descendDirectories(which, 0, path.keys[which], table)) {
                    entry.tables[which].push_back(table);
                }
            }
        }
    }
}

ArchiveFolderSet::AutoPath ArchiveFolderSet::makeAutoPath(std::wstring name) {
    const auto parts = splitFileSystemName(normalizeFileSystemName(std::move(name)));
    AutoPath path;
    for (const auto& part : parts) {
        path.keys[0].push_back(archiveNameKey(part));
        path.keys[1].push_back(archiveNameKeyUtf8(part));
    }
    return path;
}

bool ArchiveFolderSet::addAutoPath(std::wstring name) {
    auto path = makeAutoPath(std::move(name));
    for (const auto& existing : auto_paths_) {
        if (existing.keys[0] == path.keys[0] && existing.keys[1] == path.keys[1]) {
            return false;
        }
    }
    auto_paths_.push_back(std::move(path));
    applyAutoPaths();
    return true;
}

void ArchiveFolderSet::addAutoPathForAllFolder(bool asynchronous) {
    if (!treeMode()) return;
    // 18008b7a0: collect all directory names, compare both key vectors against
    // the original path-record snapshot, append without synchronous attachment.
    const auto existing_count = auto_paths_.size();
    for (const auto& name : list(L"", 1)) {
        auto path = makeAutoPath(name);
        bool duplicate = false;
        for (std::size_t i = 0; i < existing_count; ++i) {
            if (auto_paths_[i].keys[0] == path.keys[0] && auto_paths_[i].keys[1] == path.keys[1]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) auto_paths_.push_back(std::move(path));
    }
    if (!asynchronous) {
        applyAutoPaths();
        return; // no completion-flag writes in the synchronous helper
    }
    if (job_completion_.allFinished()) return;
    releaseAddAutoPathJob();
    if (job_roots_.capacity() < archives_.size()) job_roots_.reserve(archives_.size());
    const auto capacity = job_roots_.capacity();
    for (std::size_t i = 0; i < capacity; ++i) job_roots_.push_back(std::make_unique<AutoPathRootMutex>());
    launchAutoPathWorkerRanges([this] { return static_cast<std::int32_t>(auto_paths_.size()); },
        [this](std::int32_t worker, std::int32_t begin, std::int32_t end) {
            std::thread([this, worker, begin, end] {
                attachAutoPathRange(begin, end);
                job_completion_.markFinished(static_cast<std::size_t>(worker));
            }).detach();
        });
}

void ArchiveFolderSet::attachAutoPathRange(std::int32_t begin, std::int32_t end) {
    // 18008f780: each path belongs to one worker. Archives are visited in forward
    // list order; processed IDs are recorded even when the directory is absent.
    for (auto index = begin; index < end; ++index) {
        auto& path = auto_paths_[static_cast<std::size_t>(index)];
        for (std::size_t root_index = 0; root_index < archives_.size(); ++root_index) {
            auto& entry = archives_[root_index];
            if (std::find(path.processed.begin(), path.processed.end(), entry.id) != path.processed.end()) continue;
            path.processed.push_back(entry.id);
            for (int which = 0; which < 2; ++which) {
                if (path.keys[which].empty()) continue;
                std::size_t table = 0;
                if (entry.archive->descendDirectories(which, 0, path.keys[which], table)) {
                    std::lock_guard<AutoPathRootMutex> guard(*job_roots_[root_index]);
                    entry.tables[which].push_back(table);
                }
            }
        }
    }
}

bool ArchiveFolderSet::isFinishedAddAutoPathJob(bool wait) const {
    return job_completion_.isFinished(wait, [this] { return treeMode(); },
                                      [](int milliseconds) { Sleep(static_cast<DWORD>(milliseconds)); });
}

void ArchiveFolderSet::releaseAddAutoPathJob() {
    // 1800895c0: free mutex records, retain vector capacity and completion flags;
    // do not wait/join/cancel. Caller must release only after work has settled.
    job_roots_.clear();
}

bool ArchiveFolderSet::find(const std::wstring& name, bool for_get_file, Hit& hit, bool wide_keys) const {
    const auto parts = splitFileSystemName(normalizeFileSystemName(name));
    if (parts.empty()) {
        return false;
    }
    std::vector<std::uint64_t> keys;
    for (const auto& part : parts) {
        keys.push_back(wide_keys ? archiveNameKey(part) : archiveNameKeyUtf8(part));
    }
    return findKeys(keys, wide_keys ? 0 : 1, for_get_file, hit);
}

bool ArchiveFolderSet::findHashes(const std::vector<std::uint64_t>& keys, bool for_get_file, Hit& hit) const {
    return findKeys(keys, 1, for_get_file, hit);
}

bool ArchiveFolderSet::findWideHashes(const std::vector<std::uint64_t>& keys, bool for_get_file, Hit& hit) const {
    return findKeys(keys, 0, for_get_file, hit);
}

// FUN_18008db40 / FUN_18008e420 (tag 1) and FUN_18008d6e0 / FUN_18008dfc0 (tag 0) after the name has become hashes.
bool ArchiveFolderSet::findKeys(const std::vector<std::uint64_t>& keys, int which, bool for_get_file, Hit& hit) const {
    if (keys.empty()) {
        return false;
    }
    const std::vector<std::uint64_t> directories(keys.begin(), keys.end() - 1);
    const auto accepted = [&](std::uint64_t value) {
        return for_get_file ? static_cast<std::int64_t>(value) >= 0 : value != ~0ULL;
    };
    for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
        const auto& tables = it->tables[which];
        for (auto table = tables.rbegin(); table != tables.rend(); ++table) {
            std::uint64_t value = 0;
            bool found = false;
            if (keys.size() == 1) {
                found = it->archive->lookupInTable(which, *table, keys.back(), !treeMode(), value);
            } else {
                std::size_t child = 0;
                found = it->archive->descendDirectories(which, *table, directories, child) &&
                    it->archive->lookupInTable(which, child, keys.back(), false, value);
            }
            if (!found) {
                continue;
            }
            if (accepted(value)) {
                hit.archive = it->archive.get();
                hit.value = value;
                return true;
            }
            break;  // a record with a negative value ends the search in this archive
        }
    }
    return false;
}

std::wstring ArchiveFolderSet::nameOf(std::uint64_t key) const {
    for (const auto& entry : archives_) {
        if (const auto* name = entry.archive->nameMap().find(key)) {
            return *name;
        }
    }
    return {};
}

void ArchiveFolderSet::listTable(const WarcArchive& archive, std::size_t table, const std::wstring& prefix, int mode,
    std::vector<std::wstring>& out) const {
    WarcArchive::TableContents contents;
    if (!archive.readTable(0, table, contents)) {
        return;
    }
    if (mode >= 2) {
        for (const auto& file : contents.files) {
            out.push_back(prefix + nameOf(file.first));
        }
    }
    for (const auto& directory : contents.directories) {
        const std::wstring name = prefix + nameOf(directory.first) + L"\\";
        if (mode < 2) {
            out.push_back(name);
        }
        if (mode == 1 || mode == 3) {
            listTable(archive, directory.second, name, mode, out);
        }
    }
}

std::vector<std::wstring> ArchiveFolderSet::list(const std::wstring& name, int mode) const {
    std::vector<std::wstring> out;
    std::wstring prefix;
    std::vector<std::uint64_t> keys;
    const bool everything = name.empty() || name == L"*";
    if (!everything) {
        prefix = normalizeFileSystemName(name) + L"\\";
        for (const auto& part : splitFileSystemName(prefix)) {
            keys.push_back(archiveNameKey(part));
        }
    }
    for (const auto& entry : archives_) {
        std::size_t table = 0;
        if (!everything && !entry.archive->descendDirectories(0, 0, keys, table)) {
            continue;
        }
        listTable(*entry.archive, table, prefix, mode, out);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::wstring> ArchiveFolderSet::nameMapLastArchive() const {
    std::vector<std::wstring> out;
    if (archives_.empty()) {
        return out;
    }
    for (const auto* name : archives_.back().archive->nameMap().values()) {
        out.push_back(*name);
    }
    return out;
}

} // namespace cm3d2::internal
