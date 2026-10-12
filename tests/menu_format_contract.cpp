#include "menu_format.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    // Literal carrier independent of a matching serializer. Two commands,
    // little-endian version 1000, then the zero-count sentinel.
    const char fixture[] =
        "\x0a" "CM3D2_MENU" "\xe8\x03\x00\x00"
        "\x0b" "sample.menu" "\x04" "Item" "\x04" "Body" "\x04" "Info"
        "\x1d\x00\x00\x00"
        "\x02\x08" "category" "\x04" "body"
        "\x02\x05" "icons" "\x05" "a.tex" "\x00";
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(fixture);
    const std::size_t size = sizeof(fixture) - 1;
    cm3d2::internal::MenuDocument menu;
    check(cm3d2::internal::decodeMenu(bytes, size, menu), "literal menu rejected");
    check(menu.version == 1000 && menu.source_file == "sample.menu" &&
        menu.item_name == "Item" && menu.category_name == "Body" && menu.info_text == "Info",
        "header fields differ");
    check(menu.commands == std::vector<std::vector<std::string>>{
        {"category", "body"}, {"icons", "a.tex"}}, "command boundaries differ");
    check(menu.declared_command_bytes == 29 && menu.consumed_bytes == size,
        "carrier lengths differ");

    // The native reads zeroed slack past a short file: a cut inside the header still fails, a cut in the
    // command section ends the list with a zero count.
    for (std::size_t prefix = 0; prefix < 11; ++prefix) {
        menu.item_name = "unchanged";
        check(!cm3d2::internal::decodeMenu(bytes, prefix, menu), "truncated header accepted");
        check(menu.item_name == "unchanged", "failure changed output");
    }
    check(cm3d2::internal::decodeMenu(bytes, size - 1, menu), "missing terminator rejected");
    check(!cm3d2::internal::decodeMenu(nullptr, size, menu), "null input accepted");

    std::string extended(fixture, size);
    // A 128-byte source field uses the observed two-byte length 80 01.
    extended.replace(15, 12, std::string("\x80\x01", 2) + std::string(128, 'x'));
    check(cm3d2::internal::decodeMenu(
        reinterpret_cast<const std::uint8_t*>(extended.data()), extended.size(), menu) &&
        menu.source_file == std::string(128, 'x'), "two-byte length rejected");
    extended[16] = 0; // Native helper treats second bytes 0 and 1 identically.
    check(cm3d2::internal::decodeMenu(
        reinterpret_cast<const std::uint8_t*>(extended.data()), extended.size(), menu),
        "native noncanonical length behavior differs");

    extended.assign(fixture, size);
    extended.append("tail");
    check(cm3d2::internal::decodeMenu(
        reinterpret_cast<const std::uint8_t*>(extended.data()), extended.size(), menu) &&
        menu.consumed_bytes == size, "sentinel did not end parsing");
    extended[1] = 'X';
    check(!cm3d2::internal::decodeMenu(
        reinterpret_cast<const std::uint8_t*>(extended.data()), extended.size(), menu),
        "bad magic accepted");

    check(cm3d2::internal::menuNativeHash(u"sample.menu") == UINT64_C(0xc78359b04498ec46),
        "UTF-16LE filename hash differs");
    check(cm3d2::internal::menuNativeHash(u"") == UINT64_C(0xcbf29ce44fd0bfc1),
        "empty filename hash differs");
    check(cm3d2::internal::menuNativeHash(u"Sample.menu") !=
        cm3d2::internal::menuNativeHash(u"sample.menu"), "hash unexpectedly folds case");
    return 0;
}
