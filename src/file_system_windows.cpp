#include "file_system_windows.h"

#include <Windows.h>

#include "warc_archive.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>

namespace cm3d2::internal {
namespace {

constexpr std::uint64_t kEmptyHash = 0xcbf29ce44fd0bfc1ULL;

wchar_t lowerAscii(wchar_t ch) {
    return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
}

std::wstring lowered(std::wstring_view text) {
    std::wstring out(text);
    for (auto& ch : out) {
        ch = lowerAscii(ch);
    }
    return out;
}

// FUN_18006cc60: the text after the last '.', trailing spaces trimmed; empty without a '.'.
std::wstring extensionOf(const std::wstring& name) {
    const auto dot = name.rfind(L'.');
    if (dot == std::wstring::npos) {
        return {};
    }
    std::wstring extension = name.substr(dot + 1);
    while (!extension.empty() && extension.back() == L' ') {
        extension.pop_back();
    }
    return extension;
}

std::mutex g_directory_probe;

// The native tests a folder by switching the process directory to it and back.
bool directoryExists(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_directory_probe);
    std::wstring previous(32768, L'\0');
    const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(previous.size()), previous.data());
    previous.resize(length);
    const BOOL changed = SetCurrentDirectoryW(path.c_str());
    SetCurrentDirectoryW(previous.c_str());
    return changed != 0;
}

} // namespace

// FUN_18007d770: components separated by '\', empty ones dropped.
std::vector<std::wstring> splitFileSystemName(const std::wstring& normalized) {
    std::vector<std::wstring> parts;
    std::size_t at = 0;
    while (at <= normalized.size()) {
        const auto next = normalized.find(L'\\', at);
        const auto end = next == std::wstring::npos ? normalized.size() : next;
        if (end != at) {
            parts.push_back(normalized.substr(at, end - at));
        }
        if (next == std::wstring::npos) {
            break;
        }
        at = next + 1;
    }
    return parts;
}

std::wstring normalizeFileSystemName(std::wstring name) {
    for (auto& ch : name) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }
    while (!name.empty() && (name.back() == L'\\' || name.back() == L' ')) {
        name.pop_back();
    }
    for (auto& ch : name) {
        ch = lowerAscii(ch);
    }
    return name;
}

std::uint64_t fileSystemNameHash(std::wstring_view text) {
    return archiveNameKey(text);
}

bool hasDriveSeparator(const std::wstring& normalized) {
    return normalized.size() >= 2 && normalized.find(L':', 1) != std::wstring::npos;
}

bool scanFolderAllowed(std::wstring_view name) {
    if (name.empty() || name[0] != L'.') {
        return true;
    }
    const auto startsWith = [&](std::wstring_view prefix) {
        return name.size() >= prefix.size() && name.substr(0, prefix.size()) == prefix;
    };
    // Names that begin with ".works" or ".ignore" are skipped.
    return !(startsWith(L".works") || startsWith(L".ignore"));
}

const std::wstring& WindowsFolderSet::nameOf(std::uint64_t hash) const {
    static const std::wstring empty;
    const auto* found = names_.find(hash);
    return found == nullptr ? empty : *found;
}

bool WindowsFolderSet::addFolder(std::wstring name, const std::wstring& base) {
    std::wstring normalized = normalizeFileSystemName(std::move(name));
    std::wstring full = hasDriveSeparator(normalized) ? normalized : base + normalized;
    if (!directoryExists(full)) {
        return false;
    }
    const std::uint64_t hash = fileSystemNameHash(full);
    for (const auto& folder : folders_) {
        if (folder->hash == hash) {
            return false;
        }
    }
    auto folder = std::make_unique<Folder>();
    folder->hash = hash;
    folder->node.hash = hash;
    names_.tryEmplace(hash, full);
    Folder& added = *folder;
    folders_.push_back(std::move(folder));
    // The root node is the first element of the folder vector (key 0).
    added.slots.push_back(FsSlot{0, &added.node});
    // FUN_180078580 (0x1800788c8..0x1800788cf) clears +0xb8 only for a
    // newly registered folder. Existing lookup roots and bucket geometry remain.
    file_names_.clear();
    if (tree_mode_) {
        // Flag-set branch: the tree scanner starts at the root node with an empty path, the folder as its name, no name registration
        // and recursion on (0x1800789ca..0x1800789cf), then the auto paths are applied (FUN_18007e6e0).
        scanTree(added, added.node, std::wstring(), full, false, true);
        applyAutoPaths();
        return true;
    }
    // Default mode (FUN_18007c9a0): the root is scanned with an empty relative prefix whose
    // directory hash is 0.
    scan(added, full, 0, std::wstring());
    return true;
}

// FUN_18007bb80. `path` is the caller's path (empty for a root), `name` the directory name (the full folder for a root).
void WindowsFolderSet::scanTree(Folder& root, FsNode& node, std::wstring path, const std::wstring& name, bool set_name, bool recurse) {
    if (set_name) {
        node.hash = fileSystemNameHash(name);
        names_.tryEmplace(node.hash, name);
    }
    if (path.empty()) {
        path = name;
    } else {
        path += L"\\" + name;
    }
    WIN32_FIND_DATAW found{};
    HANDLE handle = FindFirstFileExW(
        (path + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            const std::wstring file_name = lowered(found.cFileName);
            const std::uint64_t key = fileSystemNameHash(file_name);
            if (node.files.find(key) != nullptr) {
                continue;  // the first file with a given name wins inside one node
            }
            FileEntry entry;
            entry.key = key;
            entry.id = root.next_id++;
            entry.extension_hash = kEmptyHash;
            entry.directory_hash = kEmptyHash;
            const std::wstring extension = extensionOf(file_name);
            if (!extension.empty()) {
                entry.extension_hash = fileSystemNameHash(extension);
                extension_names_.tryEmplace(entry.extension_hash, extension);
            }
            names_.tryEmplace(key, file_name);
            file_names_.tryEmplace(key, file_name);
            node.files.tryEmplace(key, entry);
        } else if (
            !(found.cFileName[0] == L'.' &&
              (found.cFileName[1] == L'\0' || (found.cFileName[1] == L'.' && found.cFileName[2] == L'\0'))) &&
            scanFolderAllowed(found.cFileName)) {
            const std::wstring directory = lowered(found.cFileName);
            const std::uint64_t key = fileSystemNameHash(directory);
            if (auto* existing = node.children.find(key)) {
                if (recurse) {
                    scanTree(root, **existing, path, directory, true, recurse);
                }
                continue;
            }
            auto child = std::make_unique<FsNode>();
            child->path_hashes = node.path_hashes;
            child->path_hashes.push_back(node.hash);
            if (!recurse) {
                child->hash = key;
                names_.tryEmplace(key, directory);
            } else {
                scanTree(root, *child, path, directory, true, recurse);
            }
            node.children.tryEmplace(key, std::move(child));
        }
    } while (FindNextFileW(handle, &found));
    FindClose(handle);
}

// FUN_18007e6e0.
void WindowsFolderSet::applyAutoPaths() {
    for (const auto& auto_path : auto_paths_) {
        if (auto_path.components.empty()) {
            continue;  // the original reads the first component without checking; nothing sensible to match
        }
        for (auto it = folders_.rbegin(); it != folders_.rend(); ++it) {
            Folder& root = **it;
            bool present = false;
            for (const auto& slot : root.slots) {
                if (slot.key == auto_path.hash) {
                    present = true;
                    break;
                }
            }
            if (present) {
                continue;
            }
            FsNode* current = &root.node;
            for (const auto& component : auto_path.components) {
                auto* child = current->children.find(component.first);
                if (child == nullptr) {
                    current = nullptr;
                    break;
                }
                current = child->get();
            }
            if (current != nullptr) {
                root.slots.push_back(FsSlot{auto_path.hash, current});
            }
        }
    }
}

void WindowsFolderSet::scan(Folder& folder, const std::wstring& root, std::uint64_t prefix_hash, const std::wstring& prefix) {
    std::wstring directory = root;
    if (!prefix.empty()) {
        directory += L"\\" + prefix;
    }
    WIN32_FIND_DATAW found{};
    HANDLE handle = FindFirstFileExW(
        (directory + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            const std::wstring name = lowered(found.cFileName);
            const std::uint64_t key = fileSystemNameHash(name);
            if (folder.node.files.find(key) != nullptr) {
                continue;  // the first file with a given name wins
            }
            FileEntry entry;
            entry.key = key;
            entry.id = folder.next_id++;
            const std::wstring extension = extensionOf(name);
            entry.extension_hash = fileSystemNameHash(extension);
            entry.directory_hash = prefix_hash;
            if (extension == L"menu") {
                menu_files_.insert(name);
            }
            if (!extension.empty()) {
                extension_names_.tryEmplace(entry.extension_hash, extension);
            }
            names_.tryEmplace(key, name);
            file_names_.tryEmplace(key, name);
            folder.node.files.tryEmplace(key, entry);
        } else if (
            !(found.cFileName[0] == L'.' &&
              (found.cFileName[1] == L'\0' || (found.cFileName[1] == L'.' && found.cFileName[2] == L'\0'))) &&
            scanFolderAllowed(found.cFileName)) {
            const std::wstring name = lowered(found.cFileName);
            const std::wstring next = prefix.empty() ? name : prefix + L"\\" + name;
            const std::uint64_t next_hash = fileSystemNameHash(next);
            names_.tryEmplace(next_hash, next);
            scan(folder, root, next_hash, next);
        }
    } while (FindNextFileW(handle, &found));
    FindClose(handle);
}

bool WindowsFolderSet::addAutoPath(std::wstring name) {
    const std::wstring normalized = normalizeFileSystemName(std::move(name));
    const std::uint64_t hash = fileSystemNameHash(normalized);
    for (const auto& existing : auto_paths_) {
        if (existing.hash == hash) {
            return false;
        }
    }
    AutoPath entry;
    entry.hash = hash;
    for (const auto& part : splitFileSystemName(normalized)) {
        entry.components.emplace_back(fileSystemNameHash(part), part);
    }
    auto_paths_.push_back(std::move(entry));
    // Every call applies the auto paths to the registered roots (FUN_180078cb0 ends with FUN_18007e6e0); only a tree has anything to match.
    applyAutoPaths();
    return true;
}

void WindowsFolderSet::addAutoPathForAllFolder(bool asynchronous) {
    if (!treeMode()) return;
    // 180079120 lists every directory (wide list slot 2, mode 1), snapshots the
    // existing hashes, and appends new path records without per-path attachment.
    std::vector<std::uint64_t> existing;
    existing.reserve(auto_paths_.size());
    for (const auto& path : auto_paths_) existing.push_back(path.hash);
    std::sort(existing.begin(), existing.end());
    for (const auto& name : list(L"", 1)) {
        const auto normalized = normalizeFileSystemName(name);
        const auto hash = fileSystemNameHash(normalized);
        if (std::binary_search(existing.begin(), existing.end(), hash)) continue;
        AutoPath path;
        path.hash = hash;
        for (const auto& component : splitFileSystemName(normalized))
            path.components.emplace_back(fileSystemNameHash(component), component);
        auto_paths_.push_back(std::move(path));
    }
    if (!asynchronous) {
        applyAutoPaths();
        return; // synchronous work does NOT set completion flags
    }
    if (job_completion_.allFinished()) return; // native one-shot completion guard
    releaseAddAutoPathJob();
    for (const auto& root : folders_)
        job_roots_.try_emplace(root->hash, std::make_unique<AutoPathRootMutex>());
    launchAutoPathWorkerRanges([this] { return static_cast<std::int32_t>(auto_paths_.size()); },
        [this](std::int32_t worker, std::int32_t begin, std::int32_t end) {
            std::thread([this, worker, begin, end] {
                attachAutoPathRange(begin, end);
                job_completion_.markFinished(static_cast<std::size_t>(worker));
            }).detach();
        });
}

void WindowsFolderSet::attachAutoPathRange(std::int32_t begin, std::int32_t end) {
    // 18007e300: paths in ascending range order, roots newest-first. Tree reads
    // occur outside the root lock; inspection and append use separate locks.
    for (auto index = begin; index < end; ++index) {
        const auto& path = auto_paths_[static_cast<std::size_t>(index)];
        if (path.components.empty()) continue; // retained empty-component normalization
        for (auto root_it = folders_.rbegin(); root_it != folders_.rend(); ++root_it) {
            auto& root = **root_it;
            auto* mutex = job_roots_.at(root.hash).get();
            mutex->lock();
            bool present = false;
            for (const auto& slot : root.slots) {
                if (slot.key == path.hash) { present = true; break; }
            }
            if (present) {
                // Verified 18007e4ca CALL unlock, JMP 18007e469 -> second CALL
                // unlock. Preserves the native count-underflow/hang behavior.
                mutex->unlock();
            }
            mutex->unlock();
            if (present) continue;
            FsNode* node = &root.node;
            for (const auto& component : path.components) {
                const auto* child = node->children.find(component.first);
                if (!child) { node = nullptr; break; }
                node = child->get();
            }
            if (node) {
                mutex->lock();
                root.slots.push_back(FsSlot{path.hash, node});
                mutex->unlock();
            }
        }
    }
}

bool WindowsFolderSet::isFinishedAddAutoPathJob(bool wait) const {
    return job_completion_.isFinished(wait, [this] { return treeMode(); },
                                      [](int milliseconds) { Sleep(static_cast<DWORD>(milliseconds)); });
}

void WindowsFolderSet::releaseAddAutoPathJob() {
    // 1800779f0 -> 180082780: no waiting, cancellation, or completion reset.
    job_roots_.clear();
}

namespace {

class CacheWriter {
public:
    explicit CacheWriter(const std::wstring& path) : file_(path, std::ios::binary | std::ios::out | std::ios::trunc) {}
    bool ok() const { return static_cast<bool>(file_); }
    void u64(std::uint64_t value) { file_.write(reinterpret_cast<const char*>(&value), 8); }
    void u32(std::uint32_t value) { file_.write(reinterpret_cast<const char*>(&value), 4); }
    void text(const std::wstring& value) {
        u64(value.size());
        file_.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size() * 2));
    }

private:
    std::ofstream file_;
};

class CacheReader {
public:
    explicit CacheReader(const std::wstring& path) : file_(path, std::ios::binary | std::ios::in) {
        if (file_) {
            file_.seekg(0, std::ios::end);
            remaining_ = static_cast<std::uint64_t>(file_.tellg());
            file_.seekg(0, std::ios::beg);
        }
    }
    bool open() const { return static_cast<bool>(file_); }
    bool good() const { return good_; }
    std::uint64_t u64() { std::uint64_t v = 0; raw(&v, 8); return v; }
    std::uint32_t u32() { std::uint32_t v = 0; raw(&v, 4); return v; }
    std::wstring chars(std::uint64_t count) {
        std::wstring value;
        if (count > remaining_ / 2) {
            good_ = false;
            return value;
        }
        value.resize(static_cast<std::size_t>(count));
        raw(value.data(), static_cast<std::size_t>(count) * 2);
        return value;
    }
    // A count that cannot fit in the rest of the file marks the stream bad.
    std::uint64_t count(std::uint64_t minimum_bytes_each) {
        const std::uint64_t value = u64();
        if (good_ && value > remaining_ / std::max<std::uint64_t>(1, minimum_bytes_each)) {
            good_ = false;
            return 0;
        }
        return good_ ? value : 0;
    }

private:
    void raw(void* destination, std::size_t size) {
        if (!good_ || size > remaining_) {
            good_ = false;
            std::memset(destination, 0, size);
            return;
        }
        file_.read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
        remaining_ -= size;
    }

    std::ifstream file_;
    std::uint64_t remaining_ = 0;
    bool good_ = true;
};

} // namespace

namespace {

// FUN_180077310 (child) and the inline root part of FUN_18007f450: node hash, children (key + node), files (map key, entry key,
// extension hash, id), ancestor hashes, all in container list order.
void writeNode(CacheWriter& out, const FsNode& node) {
    out.u64(node.hash);
    out.u64(node.children.size());
    node.children.forEach([&](std::uint64_t key, const std::unique_ptr<FsNode>& child) {
        out.u64(key);
        writeNode(out, *child);
    });
    out.u64(node.files.size());
    node.files.forEach([&](std::uint64_t map_key, const FsFileEntry& entry) {
        out.u64(map_key);
        out.u64(entry.key);
        out.u64(entry.extension_hash);
        out.u32(static_cast<std::uint32_t>(entry.id));
    });
    out.u64(node.path_hashes.size());
    for (const std::uint64_t hash : node.path_hashes) {
        out.u64(hash);
    }
}

// FUN_180077480. The original recurses without a bound; a cache nested deeper than this is rejected instead of overflowing the stack.
constexpr int kMaxNodeDepth = 4096;

bool readNode(CacheReader& in, FsNode& node, int depth) {
    if (depth > kMaxNodeDepth) {
        return false;
    }
    node.hash = in.u64();
    const std::uint64_t children = in.count(24);
    for (std::uint64_t i = 0; i < children && in.good(); ++i) {
        const std::uint64_t key = in.u64();
        auto child = std::make_unique<FsNode>();
        if (!readNode(in, *child, depth + 1)) {
            return false;
        }
        node.children.tryEmplace(key, std::move(child));
    }
    const std::uint64_t files = in.count(28);
    for (std::uint64_t f = 0; f < files && in.good(); ++f) {
        FsFileEntry entry;
        const std::uint64_t map_key = in.u64();
        entry.key = in.u64();
        entry.extension_hash = in.u64();
        entry.id = in.u32();
        entry.directory_hash = 0;
        node.files.tryEmplace(map_key, entry);
    }
    const std::uint64_t path_hashes = in.count(8);
    for (std::uint64_t p = 0; p < path_hashes && in.good(); ++p) {
        node.path_hashes.push_back(in.u64());
    }
    return in.good();
}

} // namespace

bool WindowsFolderSet::serialize(const std::wstring& path, const std::wstring& base_directory) const {
    CacheWriter out(path);
    if (!out.ok()) {
        return false;
    }
    out.text(base_directory);
    out.u64(folders_.size());
    for (const auto& folder : folders_) {
        out.u64(folder->hash);   // R[0]
        writeNode(out, folder->node);
        out.u32(folder->next_id);   // id counter (+0xa8)
        out.u64(folder->slots.size());
        for (const auto& slot : folder->slots) {
            out.u64(slot.key);
            // The ancestor hashes of the slot's node, then the 8 bytes AT the node pointer: the node's own hash.
            const FsNode* node = slot.node;
            out.u64(node != nullptr ? node->path_hashes.size() : 0);
            if (node != nullptr) {
                for (const std::uint64_t hash : node->path_hashes) {
                    out.u64(hash);
                }
            }
            out.u64(node != nullptr ? node->hash : 0);
        }
    }
    out.u64(auto_paths_.size());
    for (const auto& entry : auto_paths_) {
        out.u64(entry.hash);
        out.u64(entry.hash);
        out.u64(entry.components.size());
        for (const auto& component : entry.components) {
            out.u64(component.first);
            out.text(component.second);
        }
    }
    const auto write_map = [&](const MsvcOrderedNameMap& map) {
        const auto items = map.entries();  // list order, as the original writes it
        out.u64(items.size());
        for (const auto& item : items) {
            out.u64(item.first);
            out.text(*item.second);
        }
    };
    write_map(names_);
    write_map(extension_names_);
    return true;
}

bool WindowsFolderSet::deserialize(const std::wstring& path, std::wstring& base_directory) {
    CacheReader in(path);
    if (!in.open()) {
        return false;
    }
    std::wstring base = in.chars(in.count(2));
    std::vector<std::unique_ptr<Folder>> folders;
    // Folder-vector elements whose node is found after every root is read (the path of ancestor hashes, then the node's own hash).
    struct Pending {
        Folder* root;
        std::size_t slot;
        std::vector<std::uint64_t> path;
        std::uint64_t node_hash;
    };
    std::vector<Pending> pending;
    const std::uint64_t folder_count = in.count(8);
    for (std::uint64_t i = 0; i < folder_count && in.good(); ++i) {
        auto folder = std::make_unique<Folder>();
        folder->hash = in.u64();
        if (!readNode(in, folder->node, 0)) {
            return false;
        }
        folder->next_id = in.u32();
        const std::uint64_t slots = in.count(24);
        for (std::uint64_t p = 0; p < slots && in.good(); ++p) {
            FsSlot slot;
            slot.key = in.u64();
            std::vector<std::uint64_t> hashes;
            const std::uint64_t length = in.count(8);
            for (std::uint64_t e = 0; e < length && in.good(); ++e) {
                hashes.push_back(in.u64());
            }
            const std::uint64_t node_hash = in.u64();
            if (slot.key == 0) {
                slot.node = &folder->node;
            } else {
                pending.push_back(Pending{folder.get(), folder->slots.size(), std::move(hashes), node_hash});
            }
            folder->slots.push_back(slot);
        }
        folders.push_back(std::move(folder));
    }
    // Resolve the pending elements: the root whose node hash is path[0], then the children along path[1..], then the child with the
    // node's own hash. The original leaves the pointer unset when a link is missing and dereferences it later; here the slot stays empty
    // and is skipped.
    for (const auto& item : pending) {
        if (item.path.empty()) {
            continue;
        }
        for (const auto& root : folders) {
            if (root->node.hash != item.path[0]) {
                continue;
            }
            FsNode* current = &root->node;
            for (std::size_t k = 1; k < item.path.size() && current != nullptr; ++k) {
                auto* child = current->children.find(item.path[k]);
                current = child != nullptr ? child->get() : nullptr;
            }
            if (current != nullptr) {
                auto* child = current->children.find(item.node_hash);
                item.root->slots[item.slot].node = child != nullptr ? child->get() : nullptr;
            }
            break;
        }
    }
    std::vector<AutoPath> auto_paths;
    const std::uint64_t auto_count = in.count(24);
    for (std::uint64_t i = 0; i < auto_count && in.good(); ++i) {
        AutoPath entry;
        entry.hash = in.u64();
        in.u64();
        const std::uint64_t components = in.count(16);
        for (std::uint64_t c = 0; c < components && in.good(); ++c) {
            const std::uint64_t hash = in.u64();
            entry.components.emplace_back(hash, in.chars(in.count(2)));
        }
        auto_paths.push_back(std::move(entry));
    }
    const auto read_map = [&](MsvcOrderedNameMap& map) {
        map.clear();
        const std::uint64_t count = in.count(16);
        for (std::uint64_t i = 0; i < count && in.good(); ++i) {
            const std::uint64_t key = in.u64();
            map.tryEmplace(key, in.chars(in.count(2)));
        }
    };
    MsvcOrderedNameMap names;
    MsvcOrderedNameMap extensions;
    read_map(names);
    read_map(extensions);
    if (!in.good()) {
        return false;
    }
    base_directory = std::move(base);
    folders_ = std::move(folders);
    auto_paths_ = std::move(auto_paths);
    names_ = std::move(names);
    extension_names_ = std::move(extensions);
    return true;
}

// FUN_18007db30: every ancestor name followed by a backslash.
std::wstring WindowsFolderSet::ancestorPath(const std::vector<std::uint64_t>& hashes) const {
    std::wstring out;
    for (const std::uint64_t hash : hashes) {
        out += nameOf(hash) + L"\\";
    }
    return out;
}

// The two listing lambdas of FUN_180079fe0: FUN_18007a7c0 (modes below 2: sub folders, recursive for mode 1) and FUN_18007ac90
// (other modes: files, recursive into the children for mode 3).
void WindowsFolderSet::listNode(const FsNode& node, const std::wstring& prefix, int mode, std::vector<std::wstring>& out) const {
    if (mode < 2) {
        node.children.forEach([&](std::uint64_t, const std::unique_ptr<FsNode>& child) {
            std::wstring entry = prefix + nameOf(child->hash) + L"\\";
            out.push_back(entry);
            if (mode == 1) {
                listNode(*child, entry, mode, out);
            }
        });
        return;
    }
    node.files.forEach([&](std::uint64_t key, const FsFileEntry&) { out.push_back(prefix + nameOf(key)); });
    if (mode == 3) {
        node.children.forEach([&](std::uint64_t, const std::unique_ptr<FsNode>& child) {
            listNode(*child, prefix + nameOf(child->hash) + L"\\", mode, out);
        });
    }
}

std::vector<std::wstring> WindowsFolderSet::list(const std::wstring& name, int mode) const {
    std::vector<const FsNode*> targets;
    std::wstring prefix;
    if (name.empty() || name == L"*") {
        // Every root's node, last registered first.
        for (auto it = folders_.rbegin(); it != folders_.rend(); ++it) {
            targets.push_back(&(*it)->node);
        }
    } else {
        prefix = normalizeFileSystemName(name) + L"\\";
        const auto parts = splitFileSystemName(prefix);
        if (parts.empty()) {
            return {};
        }
        // The nodes the name leads to from any element of any root's folder vector (roots and elements newest first).
        for (auto it = folders_.rbegin(); it != folders_.rend(); ++it) {
            for (auto slot = (*it)->slots.rbegin(); slot != (*it)->slots.rend(); ++slot) {
                const FsNode* current = slot->node;
                for (const auto& part : parts) {
                    if (current == nullptr) {
                        break;
                    }
                    const auto* child = current->children.find(fileSystemNameHash(part));
                    current = child != nullptr ? child->get() : nullptr;
                }
                if (current != nullptr) {
                    targets.push_back(current);
                }
            }
        }
    }
    std::vector<std::wstring> out;
    for (const FsNode* target : targets) {
        listNode(*target, prefix, mode, out);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::wstring> WindowsFolderSet::fileNameMap() const {
    std::vector<std::wstring> out;
    for (const auto* name : file_names_.values()) {
        out.push_back(*name);
    }
    return out;
}

std::wstring WindowsFolderSet::resolve(const std::wstring& name) const {
    auto parts = splitFileSystemName(normalizeFileSystemName(name));
    if (parts.empty()) {
        return {};
    }
    const std::uint64_t file_key = fileSystemNameHash(parts.back());
    parts.pop_back();
    // FUN_18007e8c0 walks the root list from its last element to its first (list node +8, the previous link), so the folder
    // registered last wins for a name present in several folders (observed against the original with two folders holding the
    // same file name); inside a root it walks the folder vector from its end.
    for (auto root_it = folders_.rbegin(); root_it != folders_.rend(); ++root_it) {
        // The original keeps one result string per root and does not clear it between folder-vector elements: text appended
        // by an element that failed further down stays in front of the next element's result.
        std::wstring result;
        for (auto slot = (*root_it)->slots.rbegin(); slot != (*root_it)->slots.rend(); ++slot) {
            const FsNode* node = slot->node;
            if (node == nullptr) {
                continue;
            }
            if (parts.empty()) {
                const FsFileEntry* entry = node->files.find(file_key);
                if (entry == nullptr) {
                    continue;
                }
                result = ancestorPath(node->path_hashes) + nameOf(node->hash) + L"\\";
                if (entry->directory_hash != kEmptyHash) {
                    result += nameOf(entry->directory_hash) + L"\\";
                }
                result += nameOf(entry->key);
                return result;
            }
            const auto* first = node->children.find(fileSystemNameHash(parts.front()));
            if (first == nullptr) {
                continue;
            }
            const FsNode* current = first->get();
            if (!node->path_hashes.empty()) {
                result += ancestorPath(node->path_hashes);
            }
            result += nameOf(node->hash) + L"\\" + nameOf(current->hash) + L"\\";
            bool complete = true;
            for (std::size_t i = 1; i < parts.size(); ++i) {
                const auto* next = current->children.find(fileSystemNameHash(parts[i]));
                if (next == nullptr) {
                    complete = false;
                    break;
                }
                current = next->get();
                result += nameOf(current->hash) + L"\\";
            }
            if (!complete) {
                continue;
            }
            const FsFileEntry* entry = current->files.find(file_key);
            if (entry == nullptr) {
                break;  // every directory matched but the file is missing: the original leaves this root's element loop
            }
            result += nameOf(entry->key);
            return result;
        }
    }
    return {};
}

} // namespace cm3d2::internal
