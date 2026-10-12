#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cm3d2::internal {

// Warp archives are DLC patch archives ("warp" magic). FileSystemArchive's loader (FUN_18008ab30) turns one into a plain
// warc archive in place before mounting it: FUN_180091d90 decrypts the archive into a temporary stream, the loader copies
// that to "<path>_tmp", checks the warc magic, deletes the original and renames the temporary file over it.
//
// On-disk layout (read from FUN_180091d90, checked against the original on 74 real patch/base pairs):
//   0x00 "warp", 0x04 u32 version (1000), 0x08 u32 L (index length),
//   0x0c index (L bytes): a carrier (see decodeCarrier) under the loader's constant key; the plaintext is the file name of
//        the BASE archive this patch belongs to,
//   then u64 (read, not used), u32 N, N bytes blob 2: a carrier whose key comes from the base archive,
//   then the rest of the file, copied unchanged.
// The output file is the decrypted blob 2 followed by the rest of the file.
// The key of blob 2 is derived from the CRC-32 of the first 0x800 bytes of the base archive (0xff padded), which must
// already be mounted in the same file system under the patch's own directory or one of the preferred directories.
enum class WarpOutcome {
    NotWarp,    // the file does not start with the warp magic (or is shorter than 4 bytes): mount it as it is
    Failed,     // the archive could not be converted: AddArchive fails
    Converted,  // the file at `full_path` now is a warc archive
};

struct WarpContext {
    // File system base directory (FileSystemArchive +0x30), prefixed to the patch's own directory.
    std::wstring base_directory;
    // DLL_FileSystem_AddPatchDecryptPreferredSearchDirectory entries (lower-case), tried first.
    std::vector<std::wstring> preferred_directories;
    // FileSystemArchive slot 0x70 (FUN_18008c3e0) plus a 0x800 byte read at offset 0: copies the first bytes of the
    // mounted archive whose id (hash of its normalized path) is `id` into `head`. False when none is mounted.
    std::function<bool(std::uint64_t id, unsigned char* head, std::size_t size)> read_mounted_head;
};

// `raw_name` is the name as AddArchive received it (the directory part is taken from it), `full_path` the normalized
// absolute path the archive is opened from.
WarpOutcome convertWarpArchive(const std::wstring& raw_name, const std::wstring& full_path, const WarpContext& context);

} // namespace cm3d2::internal
