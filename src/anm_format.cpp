#include "anm_format.h"
#include "native_reader.h"

#include <cstring>
#include <string_view>
#include <utility>

namespace cm3d2::internal {
namespace {

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

// Native: the path is copied with a trailing '*' appended before searching, so
// "Bip01*" and "ManBip*" anchor those names to the end of the path.
bool boneFilterAccepts(const std::string& path) {
    const std::string starred = path + "*";
    return contains(starred, "Bip01*") || contains(starred, "_IK_") ||
        contains(starred, "ManBip*") || contains(starred, "Bone_Face");
}

bool droppedAsMune(const std::string& path, std::string_view side) {
    return contains(path, side) && !contains(path, "_IK_");
}

} // namespace

bool parseAnm(const std::uint8_t* bytes, std::size_t size, const AnmOptions& options,
    std::vector<AnmCurve>& out, std::uint32_t* version) {
    if (bytes == nullptr || size == 0) return false;
    out.clear();

    NativeReader reader(bytes, size);
    std::string magic;
    std::uint32_t file_version = 0;
    if (!reader.string(magic) || magic != "CM3D2_ANIM" || !reader.word(file_version)) {
        return false;
    }

    std::vector<AnmCurve> curves;
    std::string path;
    for (;;) {
        std::uint8_t tag = 0;
        if (!reader.byte(tag)) return false;
        if (tag == 0) break;

        if (tag == 1) {
            if (!reader.string(path)) return false;
            continue;
        }
        if (tag < 100) continue; // the native consumes no payload for these tags

        std::uint32_t raw_count = 0;
        if (!reader.word(raw_count)) return false;
        const auto count = static_cast<std::int32_t>(raw_count);
        if (count < 0) return false; // native: length_error, never returns
        std::vector<AnmKeyFrame> frames(static_cast<std::size_t>(count));
        if (count > 0) {
            const std::uint8_t* raw = reader.take(static_cast<std::size_t>(count) * 16);
            if (raw == nullptr) return false;
            std::memcpy(frames.data(), raw, frames.size() * sizeof(AnmKeyFrame));
        }

        if (!options.keep_all_bones && tag >= 0x68 && tag <= 0x6a &&
            !boneFilterAccepts(path)) {
            continue;
        }
        if (!options.keep_mune_l && droppedAsMune(path, "Mune_L")) continue;
        if (!options.keep_mune_r && droppedAsMune(path, "Mune_R")) continue;

        curves.push_back(AnmCurve{path, tag, std::move(frames)});
    }

    out = std::move(curves);
    if (version != nullptr) *version = file_version;
    return true;
}

} // namespace cm3d2::internal
