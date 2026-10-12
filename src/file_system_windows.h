#pragma once

#include <cstdint>
#include <list>

#include "msvc_ordered_map.h"
#include "file_system_job_state.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cm3d2::internal {

// FUN_180084100: '/' becomes '\', trailing '\' and ' ' are trimmed, ASCII A-Z is lowercased.
std::wstring normalizeFileSystemName(std::wstring name);

// FUN_18007d770: the non-empty components of a normalized name, split on backslashes.
std::vector<std::wstring> splitFileSystemName(const std::wstring& normalized);

// Native name hash (FNV-1a 64 over the UTF-16LE bytes, folded). The empty string hashes to
// 0xcbf29ce44fd0bfc1, the sentinel the lookup code compares against.
std::uint64_t fileSystemNameHash(std::wstring_view text);

// True when the name has ':' at index 1 or later (FUN_1800781d0, FUN_180078580, FUN_18008ab30).
bool hasDriveSeparator(const std::wstring& normalized);

// FUN_180077a20: the folder-name filter used while scanning.
bool scanFolderAllowed(std::wstring_view name);

// One entry of a node's file map (FUN_180075470/FUN_1800763f0 value, 0x20 bytes). `directory_hash` is the relative folder of the
// flat default scan; the tree scanner leaves it at the empty-string hash.
struct FsFileEntry {
    std::uint64_t key = 0;
    std::uint64_t extension_hash = 0;
    std::uint64_t id = 0;
    std::uint64_t directory_hash = 0;
};

// A scan-tree node (the object FUN_180077480 reads): its own hash, the child nodes and the files of one directory, both in
// std::unordered_map list order, and the hashes of its ancestors (+0x88). Default mode uses only the root node.
struct FsNode {
    std::uint64_t hash = 0;
    MsvcOrderedU64Map<std::unique_ptr<FsNode>> children;
    MsvcOrderedU64Map<FsFileEntry> files;
    std::vector<std::uint64_t> path_hashes;
};

// An element of a root's folder vector (+0xb0): a search start node, keyed by the auto path that selected it (0 for the root).
struct FsSlot {
    std::uint64_t key = 0;
    FsNode* node = nullptr;
};

// A registered root (the 0xc8-byte object): hash, root node, id counter (+0xa8) and the folder vector.
struct FsRoot {
    std::uint64_t hash = 0;
    std::uint32_t next_id = 0;
    FsNode node;
    std::vector<FsSlot> slots;
};

// FileSystemWindows (constructor FUN_180077dc0). The scanner mode flag (+0x190) is false for every file system created through the
// exports: folders are scanned once, flattened and searched by bare file name. The flag-set tree mode (FUN_18007bb80) is reachable
// only through the native virtual-base setter and is reconstructed for cache files and for tests (setTreeMode).
class WindowsFolderSet {
public:
    // FUN_180078580. `base` is the file system's base directory (already ends with '\').
    // The scanner mode flag at +0x190 (set through the virtual-base interface in the original; nothing in the exports sets it).
    void setTreeMode(bool on) { tree_mode_ = on; }
    bool treeMode() const { return tree_mode_; }

    bool addFolder(std::wstring name, const std::wstring& base);

    // FUN_180078cb0: registers a normalized path in the auto-path list. False for a duplicate.
    bool addAutoPath(std::wstring name);

    // FUN_180079120 / manager 18007e110. Caller must not mutate/delete the
    // filesystem or release its root records while detached work is running.
    void addAutoPathForAllFolder(bool asynchronous);
    bool isFinishedAddAutoPathJob(bool wait) const;
    void releaseAddAutoPathJob();

    // FUN_180074da0: the folder list is not empty.
    bool valid() const { return !folders_.empty(); }

    // FUN_18007e8c0: resolves a file name to a path, empty when it is not found.
    std::wstring resolve(const std::wstring& name) const;

    // The menu-name set (+0x198) in iteration order (FUN_18006e9fc).
    std::vector<std::wstring> menuFiles() const { return menu_files_.values(); }

    // FUN_180079fe0: sorted, unique names. In default mode only "" and "*" match anything and
    // only a mode of 2 or more lists files (smaller modes list sub-folders, which do not exist).
    std::vector<std::wstring> list(const std::wstring& name, int mode) const;

    // FUN_18007f450 (DLL_FileSystem_CasheSerialize): writes the base directory, the scanned
    // folders, the auto paths and the two name maps. Returns false when the file cannot be created.
    bool serialize(const std::wstring& path, const std::wstring& base_directory) const;
    // FUN_18007fe80 (DLL_FileSystem_CasheDeserialize): replaces that state from a cache file.
    bool deserialize(const std::wstring& path, std::wstring& base_directory);

    // FUN_18007b5a0: the file-name map (+0xb8) in iteration order.
    std::vector<std::wstring> fileNameMap() const;
    const MsvcOrderedNameMap& rawFileNames() const { return file_names_; }
    const MsvcOrderedStringSet& rawMenuNames() const { return menu_files_; }

private:
    using FileEntry = FsFileEntry;
    using Folder = FsRoot;

    struct AutoPath {
        std::uint64_t hash = 0;
        std::vector<std::pair<std::uint64_t, std::wstring>> components;
    };

    void scan(Folder& folder, const std::wstring& root, std::uint64_t prefix_hash, const std::wstring& prefix);
    // FUN_18007bb80 (tree mode): scans path/name into `node`; `set_name` registers the node's hash/name, `recurse` descends.
    void scanTree(Folder& root, FsNode& node, std::wstring path, const std::wstring& name, bool set_name, bool recurse);
    // FUN_18007e6e0: gives every root a folder-vector slot for each auto path that exists as a node chain below it.
    void applyAutoPaths();
    void attachAutoPathRange(std::int32_t begin, std::int32_t end);
    // FUN_18007db30: the ancestor names of a node, each followed by a backslash.
    std::wstring ancestorPath(const std::vector<std::uint64_t>& hashes) const;
    void listNode(const FsNode& node, const std::wstring& prefix, int mode, std::vector<std::wstring>& out) const;
    const std::wstring& nameOf(std::uint64_t hash) const;

    std::atomic<bool> tree_mode_{false};  // +0x190, atomic publication substitution
    AutoPathJobCompletion job_completion_;
    std::unordered_map<std::uint64_t, std::unique_ptr<AutoPathRootMutex>> job_roots_;
    std::vector<std::unique_ptr<Folder>> folders_;
    MsvcOrderedNameMap names_;            // +0x78
    MsvcOrderedNameMap extension_names_;  // +0xf8
    MsvcOrderedNameMap file_names_;
    std::vector<AutoPath> auto_paths_;
    MsvcOrderedStringSet menu_files_;
};

} // namespace cm3d2::internal
