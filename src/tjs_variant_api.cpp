#include "cm3d2/tjs_variant_api.h"
#include "tjs_variant_internal.h"

#include <Windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace {

using cm3d2::internal::TjsVariant;
constexpr std::int32_t kVoid = cm3d2::internal::kTjsVoid;
constexpr std::int32_t kString = cm3d2::internal::kTjsString;
constexpr std::int32_t kInteger = cm3d2::internal::kTjsInteger;
constexpr std::int32_t kReal = cm3d2::internal::kTjsReal;

TjsVariant* value(CM3D2_TjsVariantHandle* variant) {
    return variant == nullptr ? nullptr : static_cast<TjsVariant*>(*variant);
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const char* text) {
    if (text == nullptr) return {};
    const int length = static_cast<int>(std::strlen(text));
    if (length == 0) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, length, nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, length, out.data(), size);
    return out;
}

// Mirrors CVTTSD2SI: truncation toward zero, with the x64 "integer indefinite"
// value (INT64_MIN) for NaN and out-of-range inputs.
std::int64_t truncateToInteger(double real) {
    constexpr double kLow = -9223372036854775808.0;
    constexpr double kHigh = 9223372036854775808.0;
    if (!(real >= kLow && real < kHigh)) return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(real);
}

// A number read from text or a tag: none, integer, or real.
struct Number {
    enum class Kind { None, Integer, Real };
    Kind kind = Kind::None;
    std::int64_t integer = 0;
    double real = 0.0;
};

bool isDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool isOctal(wchar_t c) { return c >= L'0' && c <= L'7'; }
bool isHex(wchar_t c) {
    return isDigit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}
bool isAsciiAlpha(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool isSpace(wchar_t c) { return c == L' ' || (c >= L'\t' && c <= L'\r'); }

// Native keyword rule (FUN_1800d58b0): the keyword must be followed by the end
// of the text or by a character that is not an ASCII letter or underscore.
bool keywordAt(const std::wstring& text, std::size_t at, const wchar_t* keyword) {
    const std::size_t length = std::wcslen(keyword);
    if (text.compare(at, length, keyword) != 0 || text.size() - at < length) return false;
    if (at + length == text.size()) return true;
    const wchar_t next = text[at + length];
    return next < 0x100 && !isAsciiAlpha(next) && next != L'_';
}

// Parses an unsigned body after any sign: keywords, 0x/0b/octal prefixes, or a
// decimal with optional fraction and exponent. Trailing text is ignored.
Number parseBody(const std::wstring& text, std::size_t i) {
    Number out;
    if (keywordAt(text, i, L"true")) { out.kind = Number::Kind::Integer; out.integer = 1; return out; }
    if (keywordAt(text, i, L"false")) { out.kind = Number::Kind::Integer; out.integer = 0; return out; }
    if (keywordAt(text, i, L"NaN")) {
        out.kind = Number::Kind::Real;
        out.real = std::numeric_limits<double>::quiet_NaN();
        return out;
    }
    if (keywordAt(text, i, L"Infinity")) {
        out.kind = Number::Kind::Real;
        out.real = std::numeric_limits<double>::infinity();
        return out;
    }

    const std::size_t n = text.size();
    if (i < n && text[i] == L'0' && i + 1 < n) {
        const wchar_t prefix = text[i + 1];
        if (prefix == L'x' || prefix == L'X' || prefix == L'b' || prefix == L'B') {
            const bool hexadecimal = prefix == L'x' || prefix == L'X';
            std::uint64_t accumulated = 0;
            std::size_t j = i + 2;
            for (; j < n && (hexadecimal ? isHex(text[j]) : (text[j] == L'0' || text[j] == L'1')); ++j) {
                const wchar_t c = text[j];
                const std::uint64_t digit = isDigit(c) ? static_cast<std::uint64_t>(c - L'0')
                    : static_cast<std::uint64_t>((c | 0x20) - L'a' + 10);
                accumulated = accumulated * (hexadecimal ? 16U : 2U) + digit;
            }
            out.kind = Number::Kind::Integer;
            out.integer = static_cast<std::int64_t>(accumulated);
            return out;
        }
        if (prefix != L'.' && prefix != L'e' && prefix != L'E') {
            // Octal: leading zero, then octal digits. Any other following character
            // gives 0, for example "08" or "0_1".
            std::uint64_t accumulated = 0;
            std::size_t j = i + 1;
            for (; j < n && isOctal(text[j]); ++j) accumulated = accumulated * 8U + static_cast<std::uint64_t>(text[j] - L'0');
            out.kind = Number::Kind::Integer;
            out.integer = static_cast<std::int64_t>(accumulated);
            return out;
        }
    }

    // Decimal: digits, optional fraction, optional exponent with digits.
    std::size_t j = i;
    std::uint64_t integer_digits = 0;
    bool any_digit = false;
    for (; j < n && isDigit(text[j]); ++j) {
        integer_digits = integer_digits * 10U + static_cast<std::uint64_t>(text[j] - L'0');
        any_digit = true;
    }
    bool is_real = false;
    if (j < n && text[j] == L'.') {
        std::size_t k = j + 1;
        bool fraction_digit = false;
        for (; k < n && isDigit(text[k]); ++k) fraction_digit = true;
        if (any_digit || fraction_digit) {
            is_real = true;
            any_digit = true;
            j = k;
        }
    }
    if (!any_digit) return out; // Kind::None
    if (j < n && (text[j] == L'e' || text[j] == L'E')) {
        std::size_t k = j + 1;
        if (k < n && (text[k] == L'+' || text[k] == L'-')) ++k;
        if (k < n && isDigit(text[k])) {
            for (; k < n && isDigit(text[k]); ++k) {}
            is_real = true;
            j = k;
        }
    }

    if (is_real) {
        std::string ascii;
        ascii.reserve(j - i);
        for (std::size_t k = i; k < j; ++k) ascii.push_back(static_cast<char>(text[k]));
        out.kind = Number::Kind::Real;
        out.real = std::strtod(ascii.c_str(), nullptr);
        return out;
    }
    out.kind = Number::Kind::Integer;
    out.integer = static_cast<std::int64_t>(integer_digits);
    return out;
}

// Mirrors FUN_1800d6010: an optional sign, optional whitespace after the sign
// (not before it), then the body. Negation wraps for integers and flips the sign
// bit for reals.
Number parseText(const std::wstring& text) {
    std::size_t i = 0;
    bool negative = false;
    if (!text.empty() && (text[0] == L'+' || text[0] == L'-')) {
        negative = text[0] == L'-';
        i = 1;
        while (i < text.size() && isSpace(text[i])) ++i;
        if (i >= text.size()) return Number{};
    }
    Number out = parseBody(text, i);
    if (negative) {
        if (out.kind == Number::Kind::Integer) {
            out.integer = static_cast<std::int64_t>(0U - static_cast<std::uint64_t>(out.integer));
        } else if (out.kind == Number::Kind::Real) {
            out.real = -out.real;
        }
    }
    return out;
}

Number numberOf(const TjsVariant& self) {
    Number out;
    const auto& native = self.native();
    switch (native.Type()) {
    case kInteger:
        out.kind = Number::Kind::Integer;
        out.integer = native.AsInteger();
        break;
    case kReal:
        out.kind = Number::Kind::Real;
        out.real = native.AsReal();
        break;
    case kString:
        out = parseText(TJS::ttstr(native).c_str());
        break;
    default:
        break;
    }
    return out;
}

// Native formats (FUN_1800eab60, FUN_1800eb1a0 with FUN_1800eacd0 specials).
std::string formatInteger(std::int64_t number) {
    return std::to_string(number);
}

std::string formatReal(double number) {
    if (std::isnan(number)) return "NaN";
    if (std::isinf(number)) return number > 0 ? "+Infinity" : "-Infinity";
    if (number == 0.0) return std::signbit(number) ? "-0.0" : "+0.0";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.15g", number);
    return buffer;
}

} // namespace

extern "C" {

void CM3D2_CALL DLL_TJSVariant_Create(CM3D2_TjsVariantHandle* out_variant) {
    if (out_variant == nullptr) return;
    *out_variant = new (std::nothrow) TjsVariant;
}

// Native: the handle is pointed at the given object without copying or ownership
// transfer. Deleting the handle afterwards frees that object.
void CM3D2_CALL DLL_TJSVariant_Reference(CM3D2_TjsVariantHandle* variant, void* value_pointer) {
    if (variant == nullptr) return;
    *variant = value_pointer;
}

// Native frees the value object and clears the caller's handle. The native's
// second clear inside Delete is redundant and not reproduced.
void CM3D2_CALL DLL_TJSVariant_Delete(CM3D2_TjsVariantHandle* variant) {
    if (variant == nullptr) return;
    delete value(variant);
    *variant = nullptr;
}

// Native FUN_1800eb5e0 sets the tag to void before releasing the payload.
void CM3D2_CALL DLL_TJSVariant_Clear(CM3D2_TjsVariantHandle* variant) {
    auto* self = value(variant);
    if (self == nullptr) return;
    self->native().Clear();
}

int32_t CM3D2_CALL DLL_TJSVariant_GetType(CM3D2_TjsVariantHandle* variant) {
    const auto* self = value(variant);
    return self == nullptr ? kVoid : static_cast<int32_t>(self->native().Type());
}

// Native (0x18006f040) returns RAX: integers as-is, reals truncated (CVTTSD2SI),
// strings parsed then converted, void 0, and object/octet conversion errors.
int64_t CM3D2_CALL DLL_TJSVariant_AsInteger(CM3D2_TjsVariantHandle* variant) {
    const auto* self = value(variant);
    if (self == nullptr) return 0;
    if (self->native().Type() == TJS::tvtObject || self->native().Type() == TJS::tvtOctet)
        return self->native().AsInteger(); // Native conversion error, not a fabricated zero.
    const Number number = numberOf(*self);
    switch (number.kind) {
    case Number::Kind::Integer: return number.integer;
    case Number::Kind::Real: return truncateToInteger(number.real);
    default: return 0;
    }
}

// Native (0x1800730e0) returns a double; the export narrows it to float.
float CM3D2_CALL DLL_TJSVariant_AsReal(CM3D2_TjsVariantHandle* variant) {
    const auto* self = value(variant);
    if (self == nullptr) return 0.0f;
    if (self->native().Type() == TJS::tvtObject || self->native().Type() == TJS::tvtOctet)
        return static_cast<float>(self->native().AsReal());
    const Number number = numberOf(*self);
    switch (number.kind) {
    case Number::Kind::Integer: return static_cast<float>(static_cast<double>(number.integer));
    case Number::Kind::Real: return static_cast<float>(number.real);
    default: return 0.0f;
    }
}

// Native copies min(length, capacity - 1) bytes of the UTF-8 text and writes a
// terminator after conversion succeeds. A conversion error leaves the buffer
// untouched. Integers and reals use
// the native formats; voids and unset values give an empty string.
void CM3D2_CALL DLL_TJSVariant_AsString(
    CM3D2_TjsVariantHandle* variant,
    char* destination,
    int32_t capacity) {
    if (destination == nullptr || capacity <= 0) return;
    const auto* self = value(variant);
    if (self == nullptr) { destination[0] = '\0'; return; }

    std::string text;
    switch (self->native().Type()) {
    case kString: text = toUtf8(TJS::ttstr(self->native()).c_str()); break;
    case kInteger: text = formatInteger(self->native().AsInteger()); break;
    case kReal: text = formatReal(self->native().AsReal()); break;
    case TJS::tvtObject:
    case TJS::tvtOctet:
        // Native FUN_180073060 stringifies closures but rejects octet conversion.
        text = toUtf8(TJS::ttstr(self->native()).c_str());
        break;
    default: destination[0] = '\0'; return;
    }

    const auto length = text.size();
    const auto copied = static_cast<std::size_t>(capacity) > length
        ? length
        : static_cast<std::size_t>(capacity - 1);
    std::memcpy(destination, text.data(), copied);
    destination[copied] = '\0';
}

void CM3D2_CALL DLL_TJSVariant_SetInteger(CM3D2_TjsVariantHandle* variant, int32_t number) {
    auto* self = value(variant);
    if (self == nullptr) return;
    self->native() = static_cast<tjs_int32>(number);
}

void CM3D2_CALL DLL_TJSVariant_SetReal(CM3D2_TjsVariantHandle* variant, float number) {
    auto* self = value(variant);
    if (self == nullptr) return;
    self->native() = static_cast<double>(number);
}

void CM3D2_CALL DLL_TJSVariant_SetString(CM3D2_TjsVariantHandle* variant, const char* utf8) {
    auto* self = value(variant);
    if (self == nullptr) return;
    const auto text = fromUtf8(utf8);
    self->native() = text.c_str();
}

} // extern "C"
