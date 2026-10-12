// Valid-input contract for the reproduced codecvt_utf8<wchar_t, 0x10ffff> facet (src/msvc_utf8_facet.h). Expectations come from the decompiled
// do_in 0x18006bd00 / do_out 0x18006bbd0, not from the C++ standard; see the project notes (not part of this repository).
#include "msvc_utf8_facet.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using cm3d2::internal::msvc_utf8::decode;
using cm3d2::internal::msvc_utf8::toUtf8;
using cm3d2::internal::msvc_utf8::toWide;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

std::wstring wide(const char* utf8, bool* ok = nullptr) {
    std::wstring out;
    const bool good = decode(utf8, std::char_traits<char>::length(utf8), &out);
    if (ok != nullptr) *ok = good;
    return out;
}

bool rejects(const char* utf8) {
    bool ok = true;
    wide(utf8, &ok);
    return !ok;
}

bool throwsBadConversion(const char* utf8) {
    try {
        toWide(utf8);
    } catch (const std::range_error& error) {
        return std::string(error.what()) == "bad conversion";
    }
    return false;
}

} // namespace

int main() {
    // Valid text the game actually uses: ASCII, accented Latin, Japanese.
    check(wide("menu\\body_01.menu") == L"menu\\body_01.menu", "ASCII");
    check(wide("caf\xc3\xa9") == L"café", "2-byte");
    check(wide("\xe3\x83\x86\xe3\x82\xaf\xe3\x82\xb9\xe3\x83\x81\xe3\x83\xa3\xe5\xa4\x89\xe6\x9b\xb4") == L"テクスチャ変更", "3-byte Japanese");
    check(wide("\xef\xbf\xbf") == L"￿" && wide("\xe0\xa0\x80") == L"ࠀ", "3-byte range ends");

    // Valid supplementary characters are ACCEPTED and reduced to their low 16 bits (the facet writes a 16-bit unit, no surrogate pair).
    check(wide("\xf0\x9f\x98\x80") == std::wstring(1, static_cast<wchar_t>(0xf600)), "U+1F600 -> 0xF600");
    check(wide("\xf0\x90\x80\x80") == std::wstring(1, static_cast<wchar_t>(0x0000)), "U+10000 -> 0x0000");
    check(wide("\xf4\x8f\xbf\xbf") == std::wstring(1, static_cast<wchar_t>(0xffff)), "U+10FFFF -> 0xFFFF");
    check(rejects("\xf4\x90\x80\x80") && rejects("\xf7\xbf\xbf\xbf"), "values above U+10FFFF are errors");
    check(rejects("\xf8\x88\x80\x80\x80") && rejects("\xfc\x84\x80\x80\x80\x80"), "5/6-byte forms above U+10FFFF are errors");

    // No overlong check: the short value decodes (including NUL), 5/6-byte padded forms too.
    check(wide("\xc0\x80") == std::wstring(1, L'\0'), "C0 80 -> U+0000");
    check(wide("\xc1\xbf") == L"\x7f", "C1 BF -> U+007F");
    check(wide("\xe0\x80\x80") == std::wstring(1, L'\0') && wide("\xe0\x81\x81") == L"A", "3-byte overlongs");
    check(wide("\xf0\x80\x80\x81") == L"\x01", "4-byte overlong");
    check(wide("\xf8\x80\x80\x80\x81") == L"\x01" && wide("\xfc\x80\x80\x80\x80\x81") == L"\x01", "5/6-byte overlongs");

    // Surrogate values pass through unchanged.
    check(wide("\xed\xa0\x80") == std::wstring(1, static_cast<wchar_t>(0xd800)) && wide("\xed\xbf\xbf") == std::wstring(1, static_cast<wchar_t>(0xdfff)), "surrogates");

    // Errors: stray continuation as a lead, bad continuation bytes, truncated sequences.
    check(rejects("\x80") && rejects("\xbf") && rejects("A\x80"), "continuation byte as a lead");
    check(rejects("\xc3\x28") && rejects("\xe3\x83\x28") && rejects("\xc3\xc3") && rejects("\xe3\x83\xc0"), "bad continuation byte");
    check(rejects("\xc3") && rejects("\xe3\x83") && rejects("\xf0\x9f\x98") && rejects("ok\xe6\x97"), "truncated sequence");
    check(throwsBadConversion("\xc3") && throwsBadConversion("\x80") && throwsBadConversion("\xf4\x90\x80\x80"), "the wrappers throw range_error(\"bad conversion\")");
    check(!throwsBadConversion("\xf0\x9f\x98\x80") && !throwsBadConversion("\xc0\x80"), "accepted forms do not throw");
    check(toWide(nullptr).empty() && toWide("").empty(), "null and empty");

    // Encoding: 1/2/3 bytes per unit, surrogate units one by one, never an error.
    check(toUtf8(L"A") == "A" && toUtf8(L"é") == "\xc3\xa9" && toUtf8(L"日本") == "\xe6\x97\xa5\xe6\x9c\xac", "BMP");
    check(toUtf8(L"\u007f\u0080߿ࠀ￿") == "\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbf", "range edges");
    check(toUtf8(std::wstring(1, static_cast<wchar_t>(0xd83d)) + static_cast<wchar_t>(0xde00)) == "\xed\xa0\xbd\xed\xb8\x80", "a surrogate pair becomes two 3-byte sequences");
    check(toUtf8(std::wstring(1, static_cast<wchar_t>(0xd800))) == "\xed\xa0\x80", "a lone surrogate");
    check(toWide(toUtf8(L"menu\\日本.menu").c_str()) == L"menu\\日本.menu", "round trip of valid BMP text");
    std::puts("file_system_narrow_conversion_contract: ok");
    return 0;
}
