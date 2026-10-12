#pragma once

// Raw images of the two MSVC hash containers the original exposes through the wide/base slots.
// Evidence: the project notes (not part of this repository). Everything is built by hand (never through std::unordered_*, whose
// private layout is toolset specific), so the bytes match the original's 14.x STL layout:
//
//   container image (0x40 bytes)          node (set: 0x30 bytes, map: 0x38 bytes)
//     +0x00 float max_load = 1.0f           +0x00 next*   (sentinel: first node)
//     +0x08 head* (sentinel node)           +0x08 prev*   (sentinel: last node)
//     +0x10 size                            set: +0x10 wstring            (32 bytes, SSO capacity 7)
//     +0x18 buckets first*                  map: +0x10 u64 key, +0x18 wstring
//     +0x20 buckets last*
//     +0x28 buckets end*   (2 pointers per bucket: lo, hi; empty bucket = sentinel)
//     +0x30 mask   (bucket_count - 1)
//     +0x38 maxidx (bucket_count)
//
// A snapshot is read-only for native consumers: they walk head->next until head and read the size at +0x10. Lookups
// through the bucket pairs are only valid when `bucket_count` equals the original's geometry (see geometryFor).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cm3d2::internal::map_abi {

inline constexpr std::size_t kImageSize = 0x40;
inline constexpr std::size_t kSetNodeSize = 0x30;
inline constexpr std::size_t kMapNodeSize = 0x38;

enum class Kind { Set, Map };   // Set = unordered_set<wstring> (FUN_18006e540), Map = unordered_map<u64, wstring> (FUN_18006e690)

// The original's bucket count after inserting `count` distinct elements into a fresh container (initial 8 buckets).
std::size_t geometryFor(std::size_t count);

enum class Status {
    Ok,
    AllocationFailed,   // nothing was published: a previously valid image is left unchanged
    BadBucketCount,     // not a power of two in [8, 2^26]
    NotContiguous,      // the order cannot be an MSVC iteration order for this bucket count (a bucket's nodes are not one run)
    DuplicateKey,       // set: equal values; map: equal keys
};

// `image` is kImageSize bytes of caller storage (e.g. inside the adapter buffer at base-0x48 or the file-name map offset).
// construct: on any failure the storage is left zeroed (destroy is then a no-op).
Status construct(void* image, Kind kind) noexcept;
void destroy(void* image, Kind kind) noexcept;

// Replace the content with `ordered`, preserving that exact list order, or change nothing. bucket_count 0 = geometryFor(size).
// Required input order: a real iteration order of the original container for that bucket count, i.e. all nodes of one bucket form a
// single run (what MsvcOrderedStringSet / MsvcOrderedNameMap produce with geometryFor(size)); anything else is rejected, never published.
Status assignSet(void* image, const std::vector<std::wstring>& ordered, std::size_t bucket_count = 0) noexcept;
Status assignMap(void* image, const std::vector<std::pair<std::uint64_t, std::wstring>>& ordered, std::size_t bucket_count = 0) noexcept;

// Reads an image the way the original's consumers do (head->next walk, size at +0x10, wstring SSO rules).
std::size_t sizeOf(const void* image);
std::vector<std::wstring> readSet(const void* image);
std::vector<std::pair<std::uint64_t, std::wstring>> readMap(const void* image);

// Hashes used for the bucket index (same as MsvcOrderedStringSet / MsvcOrderedNameMap).
std::uint64_t hashSetKey(const std::wstring& value);
std::uint64_t hashMapKey(std::uint64_t key);

} // namespace cm3d2::internal::map_abi
