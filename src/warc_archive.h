#pragma once

#include <cstddef>
#include <utility>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "msvc_ordered_map.h"

namespace cm3d2::internal {

// Key used by the native file system for one name component: FNV-1a 64 over the
// UTF-16LE bytes, folded with h ^ (h >> 32). The caller lowercases ASCII first.
std::uint64_t archiveNameKey(std::wstring_view component);
// The same hash over the UTF-8 bytes; keys the tag 1 table the narrow-string API searches.
std::uint64_t archiveNameKeyUtf8(std::wstring_view component);

struct WarcName {
    std::uint64_t key = 0;
    std::wstring name;
};

// A warc archive, reconstructed from the native loader (FUN_180089f50 and its
// helpers). Layout:
//   8-byte magic 77 61 72 63 ff aa 45 f1, u32 version (> 999), u32 section count
//   sections: { u32 tag, u64 size, payload[size] } back to back
//   tag 0: the name table (below), keyed by UTF-16 hashes; tag 1: the same index keyed by
//   UTF-8 hashes (a different folder order), searched by the narrow-string API;
//   tag 2: member data; tag 3: the name list (optional). Tags 0, 1 and 2 are required.
// A table starts with { u32 headerSize, u32 recordSize, u64 id, u32 dirCount,
// u32 fileCount, ... }. At base + headerSize come dirCount directory records
// { u64 key, u64 relative offset of a nested table }, then fileCount file records
// { u64 key, u64 value } sorted by key and searched with a lower bound. A value is
// an offset into the tag 2 payload, where a member is { u32 compress, u32 crypt,
// u32 decodedSize, u32 payloadSize } followed by the payload.
class WarcArchive {
public:
    // Returns false for files that are not warc archives or are malformed. Warp
    // archives (DLC patches) are not accepted.
    static bool open(const std::wstring& path, WarcArchive& out);

    // Looks a name up the way the native file system does: ASCII-lowercased, split on
    // '\' or '/', trailing spaces ignored. A path descends through directory records;
    // a bare name is searched through every folder.
    // `utf8_keys` selects the tag 1 table (UTF-8 hashes, used by the narrow-string API,
    // folder order differs) instead of the tag 0 table (UTF-16 hashes).
    bool find(std::wstring_view name, std::uint64_t& value, bool utf8_keys) const;

    // Reads and decodes the member at `value`: XOR (crypt 1), carrier (crypt 2), then
    // zlib (compress 1). The result has the member's declared decoded size.
    bool read(std::uint64_t value, std::vector<unsigned char>& out) const;
    // Native member factory's decoded allocation, ready for FileMemory ownership
    // transfer. No intermediate decoded vector or signed-int32 size conversion.
    bool readOwned(std::uint64_t value, std::unique_ptr<std::byte[]>& out, std::uint32_t& size) const;
    // Positioned raw read from the retained mount, preserving its cursor.
    bool readHead(unsigned char* out, std::size_t size) const;

    // The tag 3 name list (decoded on first use). Empty when absent or malformed.
    const std::vector<WarcName>& names() const;

    // The tag 3 names as the archive file system keeps them: an MSVC unordered_map keyed by the
    // tag 0 name key (FUN_180089f50 inserts them in list order).
    const MsvcOrderedNameMap& nameMap() const;

    // Table-level access used by the archive file system. `which` is 0 for the tag 0 table
    // (UTF-16 keys) or 1 for the tag 1 table (UTF-8 keys); a table is an offset into that buffer
    // and the root table is at offset 0.
    struct TableContents {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> files;   // key, value
        std::vector<std::pair<std::uint64_t, std::size_t>> directories;  // key, child table
    };
    // FUN_18008f2c0: follows directory keys one by one.
    bool descendDirectories(int which, std::size_t table, const std::vector<std::uint64_t>& keys, std::size_t& out) const;
    // FUN_180089b30: lower-bound file lookup, optionally searching every directory below.
    bool lookupInTable(int which, std::size_t table, std::uint64_t key, bool recurse, std::uint64_t& value) const;
    bool readTable(int which, std::size_t table, TableContents& out) const;

private:
    struct Section {
        std::uint64_t payload = 0;  // absolute offset of the payload
        std::uint64_t size = 0;
    };

    bool lookupFile(int which, std::size_t table, std::uint64_t key, std::uint64_t& value, bool recurse) const;
    bool lookupDirectory(int which, std::size_t table, std::uint64_t key, std::size_t& nested) const;
    bool tableValid(int which, std::size_t table) const;
    bool readSection(const Section& section, std::vector<unsigned char>& out) const;
    bool readSectionOwned(const Section& section, std::unique_ptr<std::byte[]>& out, std::uint32_t& size) const;

    std::wstring path_;
    struct MountedStream;
    std::shared_ptr<MountedStream> stream_; // retained Win32File-style read-only mount
    std::uint64_t file_size_ = 0;
    Section data_;                      // tag 2
    Section names_section_;             // tag 3 (size 0 when absent)
    bool has_names_ = false;
    std::vector<unsigned char> tables_[2];  // tag 0 (UTF-16 keys) and tag 1 (UTF-8 keys) payloads
    mutable bool names_loaded_ = false;
    mutable std::vector<WarcName> names_;
    mutable bool name_map_built_ = false;
    mutable MsvcOrderedNameMap name_map_;
};

} // namespace cm3d2::internal
