#pragma once

// The original's std::wstring_convert<std::codecvt_utf8<wchar_t, 0x10ffff, 0>> (default constructed: no error strings), reproduced from
// do_in 0x18006bd00, do_out 0x18006bbd0 and the wrapper loops FUN_1800711c0 / FUN_18006c3f0 (docs/filesystem-conversion-facet-review.md).
//
// Decoding (UTF-8 -> 16-bit wchar_t), exactly as the binary does it:
//   * lead < 0x80: one unit; 0x80..0xBF as a lead: error; < 0xE0: 1 continuation; < 0xF0: 2; < 0xF8: 3; >= 0xF8: 4 (< 0xFC) or 5.
//   * every continuation byte must be 0x80..0xBF; there is NO overlong check ("C0 80" decodes to U+0000, "E0 80 80" too);
//   * a value above 0x10FFFF is an error; anything else is stored as its LOW 16 BITS (U+1F600 -> 0xF600, U+10000 -> 0x0000, U+10FFFF -> 0xFFFF);
//     surrogate values (ED A0 80 ...) pass through unchanged;
//   * a sequence cut off by the end of the input leaves the converter without progress and the wrapper loop ends in "bad conversion".
// Encoding (wchar_t -> UTF-8): < 0x80 one byte, < 0x800 two, otherwise three bytes; surrogate code units are encoded individually
// (ED A0 80 style); this direction never fails.
// Failure in either direction throws std::range_error("bad conversion") in the wrappers (no error strings are configured).

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace cm3d2::internal::msvc_utf8 {

// Returns false on an error byte or a truncated sequence; `out` (may be null) receives the units decoded up to that point.
inline bool decode(const char* bytes, std::size_t size, std::wstring* out) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes);
    const auto* end = p + size;
    while (p != end) {
        const unsigned lead = *p;
        std::uint32_t value = lead;
        if (lead < 0x80) {
            ++p;
        } else {
            if (lead < 0xc0) {
                return false;
            }
            int continuation;
            if (lead < 0xe0) { value = lead & 0x1fU; continuation = 1; }
            else if (lead < 0xf0) { value = lead & 0x0fU; continuation = 2; }
            else if (lead < 0xf8) { value = lead & 0x07U; continuation = 3; }
            else { value = lead & 0x03U; continuation = lead < 0xfc ? 4 : 5; }
            if (end - p < static_cast<std::ptrdiff_t>(continuation) + 1) {
                return false;   // incomplete trailing sequence: the original ends in "bad conversion"
            }
            ++p;
            for (int i = 0; i < continuation; ++i, ++p) {
                if (*p < 0x80 || *p > 0xbf) {
                    return false;
                }
                value = (*p & 0x3fU) | (value << 6);
            }
        }
        if (value > 0x10ffffU) {
            return false;
        }
        if (out != nullptr) {
            out->push_back(static_cast<wchar_t>(static_cast<std::uint16_t>(value)));
        }
    }
    return true;
}

// A null pointer is the empty string; the input ends at the first NUL (the callers pass strlen-bounded ranges).
inline std::wstring toWide(const char* utf8) {
    std::wstring out;
    if (utf8 == nullptr) {
        return out;
    }
    std::size_t size = 0;
    while (utf8[size] != '\0') {
        ++size;
    }
    if (!decode(utf8, size, &out)) {
        throw std::range_error("bad conversion");
    }
    return out;
}

inline std::string toUtf8(const std::wstring& wide) {
    std::string out;
    out.reserve(wide.size());
    for (const wchar_t unit : wide) {
        const unsigned value = static_cast<std::uint16_t>(unit);
        if (value < 0x80) {
            out.push_back(static_cast<char>(value));
        } else if (value < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (value >> 6)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xe0 | (value >> 12)));
            out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }
    return out;
}

} // namespace cm3d2::internal::msvc_utf8
