#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cm3d2::internal {

// Internal format reconstruction, not an additional DLL ABI. Strings preserve
// their on-disk bytes; command interpretation and map resolution are separate.
struct MenuDocument {
    std::uint32_t version = 0;
    std::string source_file;
    std::string item_name;
    std::string category_name;
    std::string info_text;
    std::uint32_t declared_command_bytes = 0;
    std::vector<std::vector<std::string>> commands;
    std::size_t consumed_bytes = 0;
};

// Bounds rejection is deliberate hardening beyond the native unchecked reader.
// Failure leaves output unchanged. Native parsing ends at the zero count byte;
// the declared byte count and any trailing bytes are not enforced here.
bool decodeMenu(const std::uint8_t* bytes, std::size_t size, MenuDocument& output);

// The native loader hashes the UTF-16LE basename without case normalization.
std::uint64_t menuNativeHash(std::u16string_view basename);

} // namespace cm3d2::internal
