#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cm3d2::internal {

// Reconstruction of FUN_180065b60 (0x180065b60), the CM3D2_ANIM reader behind
// DLL_AnmParse_Open/OpenByte. Inferred source, not original source.
//
// Wire format: length-prefixed "CM3D2_ANIM", uint32 version (ignored by the
// native), then a tag stream ended by tag 0:
//   tag 1        length-prefixed path string; becomes the current path
//   tag >= 100   int32 count, then count * 16 raw bytes (key frames); appends
//                a curve {current path, tag, key frames} unless filtered
//   tag 2..99    no payload; skipped
struct AnmKeyFrame {
    // The native copies these 16 bytes opaquely. The float roles follow the
    // Unity Keyframe layout and are inferred, not observed.
    float time;
    float value;
    float in_tangent;
    float out_tangent;
};
static_assert(sizeof(AnmKeyFrame) == 16, "native key frames are 0x10 bytes");

struct AnmCurve {
    std::string path;
    std::uint32_t com_id = 0; // the tag byte (>= 100) that introduced the curve
    std::vector<AnmKeyFrame> key_frames;
};

// Flag names describe observed effects; the native parameters are unnamed.
// parser arg 4 = keep_mune_l, arg 5 = keep_mune_r, arg 6 = keep_all_bones.
struct AnmOptions {
    // When false, curves whose path contains "Mune_L" and not "_IK_" are dropped.
    bool keep_mune_l = false;
    // When false, curves whose path contains "Mune_R" and not "_IK_" are dropped.
    bool keep_mune_r = false;
    // When false, tags 104..106 are kept only for paths that end in "Bip01",
    // contain "_IK_", end in "ManBip", or contain "Bone_Face".
    bool keep_all_bones = false;
};

// Null/empty input returns false and leaves `out` untouched. Otherwise `out` is
// cleared first (as the native does) and holds the curves on success. Bounds
// rejection (truncation, negative key counts, missing terminator tag) is
// deliberate hardening: the native reads unchecked. A failed parse leaves `out`
// empty.
bool parseAnm(const std::uint8_t* bytes, std::size_t size, const AnmOptions& options,
    std::vector<AnmCurve>& out, std::uint32_t* version = nullptr);

} // namespace cm3d2::internal
