// Platform hooks for the vendored TJS2 configuration. Upstream normally uses
// its own CP932 tables; these Windows conversions are a compatibility bridge,
// not a claim that every nonstandard CP932 mapping matches those tables.
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <limits>
#include "tjsTypes.h"

bool IsSJISLeadByte(tjs_nchar b) {
    const auto value = static_cast<unsigned char>(b);
    return (value >= 0x81 && value <= 0x9f) || (value >= 0xe0 && value <= 0xfc);
}

tjs_size SJISToUnicodeString(const char* input, tjs_char* output, tjs_size limit) {
    if (!input) return 0;
    tjs_size count = 0;
    while (*input && (!output || count < limit)) {
        const int bytes = IsSJISLeadByte(*input) && input[1] ? 2 : 1;
        wchar_t character = 0;
        if (!MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, input, bytes, &character, 1))
            return static_cast<tjs_size>(-1);
        if (output) output[count] = character;
        ++count;
        input += bytes;
    }
    return count;
}

tjs_size SJISToUnicodeString(const char* input, tjs_char* output) {
    return SJISToUnicodeString(input, output, (std::numeric_limits<tjs_size>::max)());
}

tjs_uint UnicodeToSJIS(tjs_char input) {
    char bytes[2]{};
    BOOL substituted = FALSE;
    const int count = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, &input, 1,
                                         bytes, 2, nullptr, &substituted);
    if (!count || substituted) return 0;
    return count == 1 ? static_cast<unsigned char>(bytes[0]) :
        (static_cast<unsigned char>(bytes[0]) << 8) | static_cast<unsigned char>(bytes[1]);
}

tjs_size UnicodeToSJISString(const tjs_char* input, tjs_nchar* output, tjs_size limit) {
    if (!input) return 0;
    tjs_size count = 0;
    while (*input) {
        const tjs_uint character = UnicodeToSJIS(*input++);
        if (!character) return static_cast<tjs_size>(-1);
        const tjs_size bytes = character > 0xff ? 2 : 1;
        if (output && bytes > limit - count) break;
        if (output) {
            if (bytes == 2) output[count] = static_cast<char>(character >> 8);
            output[count + bytes - 1] = static_cast<char>(character & 0xff);
        }
        count += bytes;
    }
    return count;
}

tjs_size UnicodeToSJISString(const tjs_char* input, tjs_nchar* output) {
    return UnicodeToSJISString(input, output, (std::numeric_limits<tjs_size>::max)());
}
