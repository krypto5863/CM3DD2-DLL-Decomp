#include "menu_format.h"
#include "native_reader.h"

#include <utility>

namespace cm3d2::internal {
namespace {

using Reader = NativeReader;

} // namespace

bool decodeMenu(const std::uint8_t* bytes, std::size_t size, MenuDocument& output) {
    if (bytes == nullptr || size == 0) return false;
    // The native analyser has no bounds checks and reads the zeroed slack after the file buffer, so a
    // file cut short simply ends with count 0 / empty strings. Reads beyond this slack fail (hardening).
    std::vector<std::uint8_t> padded(bytes, bytes + size);
    padded.resize(size + 65536, 0);
    Reader reader(padded.data(), padded.size());
    MenuDocument result;
    std::string magic;
    if (!reader.string(magic) || magic != "CM3D2_MENU" ||
        !reader.word(result.version) || !reader.string(result.source_file) ||
        !reader.string(result.item_name) || !reader.string(result.category_name) ||
        !reader.string(result.info_text) || !reader.word(result.declared_command_bytes)) {
        return false;
    }

    for (;;) {
        std::uint8_t count = 0;
        if (!reader.byte(count)) return false;
        if (count == 0) break;
        std::vector<std::string> command(count);
        for (auto& token : command) {
            if (!reader.string(token)) return false;
        }
        result.commands.push_back(std::move(command));
    }
    result.consumed_bytes = reader.position();
    output = std::move(result);
    return true;
}

std::uint64_t menuNativeHash(std::u16string_view basename) {
    std::uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (const char16_t unit : basename) {
        hash = (hash ^ (static_cast<std::uint16_t>(unit) & 0xff)) * UINT64_C(0x100000001b3);
        hash = (hash ^ (static_cast<std::uint16_t>(unit) >> 8)) * UINT64_C(0x100000001b3);
    }
    return hash ^ (hash >> 32);
}

} // namespace cm3d2::internal
