#include "cm3d2/tjs_variant_api.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    CM3D2_TjsVariantHandle variant = nullptr;
    DLL_TJSVariant_Create(&variant);
    require(variant != nullptr, "creation returned null");
    require(DLL_TJSVariant_GetType(&variant) == 0, "new variant should be void");
    require(DLL_TJSVariant_AsInteger(&variant) == 0, "void should read as integer 0");
    require(DLL_TJSVariant_AsReal(&variant) == 0.0f, "void should read as real 0");
    char text[8] = "junk";
    DLL_TJSVariant_AsString(&variant, text, static_cast<int32_t>(sizeof(text)));
    require(text[0] == '\0', "void should read as empty string");

    // Integer: stored sign-extended; real and string views follow the native rules.
    DLL_TJSVariant_SetInteger(&variant, -5);
    require(DLL_TJSVariant_GetType(&variant) == 4, "integer tag mismatch");
    require(DLL_TJSVariant_AsInteger(&variant) == -5, "integer payload mismatch");
    require(DLL_TJSVariant_AsReal(&variant) == -5.0f, "integer to real mismatch");

    // Real: AsInteger truncates toward zero, as CVTTSD2SI does.
    DLL_TJSVariant_SetReal(&variant, 2.75f);
    require(DLL_TJSVariant_GetType(&variant) == 5, "real tag mismatch");
    require(DLL_TJSVariant_AsInteger(&variant) == 2, "positive truncation mismatch");
    require(DLL_TJSVariant_AsReal(&variant) == 2.75f, "real payload mismatch");
    DLL_TJSVariant_SetReal(&variant, -2.75f);
    require(DLL_TJSVariant_AsInteger(&variant) == -2, "negative truncation mismatch");

    // Out-of-range or NaN reals give the x64 integer-indefinite value.
    DLL_TJSVariant_SetReal(&variant, 1.0e30f);
    require(DLL_TJSVariant_AsInteger(&variant) == INT64_MIN, "overflow should be indefinite");

    // String: UTF-8 in, UTF-8 out, truncated to capacity - 1 bytes.
    DLL_TJSVariant_SetString(&variant, "abcdef");
    require(DLL_TJSVariant_GetType(&variant) == 2, "string tag mismatch");
    char small[5] = {};
    DLL_TJSVariant_AsString(&variant, small, static_cast<int32_t>(sizeof(small)));
    require(std::strcmp(small, "abcd") == 0, "truncation mismatch");
    char roomy[16] = {};
    DLL_TJSVariant_AsString(&variant, roomy, static_cast<int32_t>(sizeof(roomy)));
    require(std::strcmp(roomy, "abcdef") == 0, "full string mismatch");
    DLL_TJSVariant_SetString(&variant, "h\xc3\xa9llo");
    char accented[16] = {};
    DLL_TJSVariant_AsString(&variant, accented, static_cast<int32_t>(sizeof(accented)));
    require(std::strcmp(accented, "h\xc3\xa9llo") == 0, "UTF-8 round trip mismatch");
    DLL_TJSVariant_SetString(&variant, nullptr);
    char empty_string[4] = "xx";
    DLL_TJSVariant_AsString(&variant, empty_string, static_cast<int32_t>(sizeof(empty_string)));
    require(empty_string[0] == '\0', "null string should read as empty");

    // String conversions follow the native parser (checked differentially against the original DLL).
    const struct { const char* text; int64_t integer; } parses[] = {
        {"123", 123}, {"-5", -5}, {"+ 7", 7}, {"-	5", -5}, {"1.5", 1}, {"0x1F", 31}, {"010", 8},
        {"08", 0}, {"true", 1}, {"false", 0}, {"truex", 0}, {"12abc", 12}, {"1_000", 1}, {"  42", 0},
        {"00012", 10}, {"1e3", 1000}, {"1e", 1}, {"1e5.5", 100000}, {"0xFFFFFFFFFFFFFFFF", -1},
        {"9223372036854775808", INT64_MIN}, {"1e400", INT64_MIN}, {"-", 0}, {"+-5", 0}, {"e5", 0},
    };
    for (const auto& p : parses) {
        DLL_TJSVariant_SetString(&variant, p.text);
        require(DLL_TJSVariant_AsInteger(&variant) == p.integer, "string integer parse mismatch");
    }
    DLL_TJSVariant_SetString(&variant, "1E-2");
    require(DLL_TJSVariant_AsReal(&variant) == 0.01f, "string real parse mismatch");

    // Integer and real text use the native formats.
    char rendered[32];
    DLL_TJSVariant_SetInteger(&variant, -2147483647 - 1);
    DLL_TJSVariant_AsString(&variant, rendered, static_cast<int32_t>(sizeof(rendered)));
    require(std::strcmp(rendered, "-2147483648") == 0, "integer text mismatch");
    DLL_TJSVariant_SetReal(&variant, 0.0f);
    DLL_TJSVariant_AsString(&variant, rendered, static_cast<int32_t>(sizeof(rendered)));
    require(std::strcmp(rendered, "+0.0") == 0, "zero text mismatch");
    DLL_TJSVariant_SetReal(&variant, 1.5f);
    DLL_TJSVariant_AsString(&variant, rendered, static_cast<int32_t>(sizeof(rendered)));
    require(std::strcmp(rendered, "1.5") == 0, "real text mismatch");

    // Clear resets the tag to void.
    DLL_TJSVariant_Clear(&variant);
    require(DLL_TJSVariant_GetType(&variant) == 0, "clear should set void");

    // Reference points a second handle at the same object without copying.
    DLL_TJSVariant_SetInteger(&variant, 7);
    CM3D2_TjsVariantHandle alias = variant;
    require(DLL_TJSVariant_GetType(&alias) == 4 && DLL_TJSVariant_AsInteger(&alias) == 7,
        "reference should read the same object");
    CM3D2_TjsVariantHandle other = nullptr;
    DLL_TJSVariant_Reference(&other, variant);
    require(DLL_TJSVariant_AsInteger(&other) == 7, "reference target mismatch");
    other = nullptr; // not owned; the original handle frees the object below

    DLL_TJSVariant_Delete(&variant);
    require(variant == nullptr, "delete should clear the handle");

    // Null handles and outputs are ignored.
    DLL_TJSVariant_Delete(nullptr);
    DLL_TJSVariant_SetInteger(nullptr, 1);
    DLL_TJSVariant_Clear(nullptr);
    require(DLL_TJSVariant_GetType(nullptr) == 0, "null handle should read as void");
    return 0;
}
