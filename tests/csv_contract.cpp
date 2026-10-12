#include "cm3d2/csv_api.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

std::vector<std::uint8_t> makeDecodedTable() {
    std::vector<std::uint8_t> bytes;
    appendU32(bytes, 0xff767377U);
    appendU32(bytes, 3);
    appendU32(bytes, 2);

    const std::uint32_t descriptors[][2] = {
        {0, 3}, {3, 4}, {7, 2}, {9, 1}, {10, 0}, {10, 2}
    };
    for (const auto& descriptor : descriptors) {
        appendU32(bytes, descriptor[0]);
        appendU32(bytes, descriptor[1]);
    }

    const char blob[] = {'4', '2', '\0', '3', '.', '5', '\0', '1', '\0', '\0', '0', '\0'};
    bytes.insert(bytes.end(), blob, blob + sizeof(blob));
    return bytes;
}

std::vector<std::uint8_t> makeEncryptedCarrier(const std::vector<std::uint8_t>& plain) {
    constexpr std::uint32_t seed = 0x12345678U;
    constexpr std::uint8_t pad_count = 8;
    constexpr std::uint8_t encoded_seed_low = 0x78U ^ 0xbfU;
    constexpr std::uint8_t iv[16] = {
        0x4e, 0x02, 0xde, 0xcb, 0xad, 0x53, 0x2c, 0x0c,
        0x0e, 0x0e, 0x6d, 0x82, 0xad, 0x0f, 0x5a, 0x32
    };
    constexpr std::uint8_t key_bytes[16] = {
        0xaa, 0xc9, 0xd2, 0x35, 0x22, 0x87, 0x20, 0xf2,
        0x40, 0xc5, 0x61, 0x7c, 0x01, 0xdf, 0x66, 0x54
    };

    std::vector<std::uint8_t> padded = plain;
    padded.resize((plain.size() + 15U) & ~std::size_t{15U}, 0);
    require(padded.size() - plain.size() == pad_count,
        "fixture padding should exercise trailer trimming");

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0,
        "AES provider setup failed");
    require(BCryptSetProperty(
        algorithm,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
        static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)),
        0) >= 0,
        "CBC mode setup failed");

    DWORD object_size = 0;
    DWORD result_size = 0;
    require(BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size),
        sizeof(object_size),
        &result_size,
        0) >= 0,
        "AES key object query failed");
    std::vector<UCHAR> key_object(object_size);
    require(BCryptGenerateSymmetricKey(
        algorithm,
        &key,
        key_object.data(),
        object_size,
        const_cast<PUCHAR>(key_bytes),
        sizeof(key_bytes),
        0) >= 0,
        "AES key setup failed");

    std::vector<std::uint8_t> carrier(padded.size() + 5);
    ULONG written = 0;
    auto iv_copy = std::vector<std::uint8_t>(std::begin(iv), std::end(iv));
    const auto status = BCryptEncrypt(
        key,
        padded.data(),
        static_cast<ULONG>(padded.size()),
        nullptr,
        iv_copy.data(),
        static_cast<ULONG>(iv_copy.size()),
        carrier.data(),
        static_cast<ULONG>(padded.size()),
        &written,
        0);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    require(status >= 0 && written == padded.size(), "AES fixture encryption failed");

    const auto trailer_offset = padded.size();
    carrier[trailer_offset] = pad_count ^ encoded_seed_low;
    const auto encoded_seed = seed ^ 0xbfbfbfbfU;
    for (std::size_t i = 0; i < sizeof(encoded_seed); ++i) {
        carrier[trailer_offset + 1 + i] =
            static_cast<std::uint8_t>(encoded_seed >> (i * 8));
    }
    return carrier;
}

} // namespace

int main() {
    auto bytes = makeDecodedTable();
    CM3D2_FileHandle file = nullptr;
    DLL_FileMemory_CreateileMemory(&file);
    const auto carrier = makeEncryptedCarrier(bytes);
    DLL_FileMemory_SetData(&file, carrier.data(), static_cast<std::int32_t>(carrier.size()));

    auto csv = DLL_CSV_CreateCsvParser();
    require(csv != nullptr, "CSV creation failed");
    require(DLL_CSV_IsValid(csv) == 0, "new CSV parser should be invalid");
    DLL_CSV_Open(csv, DLL_FileMemory_GetInterfaceFilePointer(&file));

    require(DLL_CSV_IsValid(csv) == 1, "decoded table should be valid");
    require(DLL_CSV_GetMaxCellX(csv) == 3, "CSV width mismatch");
    require(DLL_CSV_GetMaxCellY(csv) == 2, "CSV height mismatch");
    require(DLL_CSV_GetCellAsInteger(csv, 0, 0) == 42, "integer conversion mismatch");
    require(std::fabs(DLL_CSV_GetCellAsReal(csv, 1, 0) - 3.5F) < 0.0001F,
        "real conversion mismatch");
    require(DLL_CSV_GetCellBool(csv, 2, 0) == 1, "true conversion mismatch");
    require(DLL_CSV_GetCellBool(csv, 2, 1) == 0, "false conversion mismatch");

    require(DLL_CSV_IsCellToExistData(csv, 0, 1) == 1,
        "empty string should still exist");
    require(DLL_CSV_IsCellToExistData(csv, 1, 1) == 0,
        "zero-sized descriptor should not exist");
    require(DLL_CSV_IsCellToExistData(csv, -1, 0) == 0,
        "negative coordinate should not exist");
    require(DLL_CSV_GetDataSizeString(csv, 0, 0) == 3,
        "string size should include terminator");
    require(DLL_CSV_GetDataSizeBinary(csv, 0, 0) == 2,
        "binary size should exclude terminator");

    char text[4] = {'x', 'x', 'x', 'x'};
    DLL_CSV_GetCellAsString(csv, 0, 0, text, 4);
    require(std::strcmp(text, "42") == 0, "string copy mismatch");
    char truncated[2]{};
    DLL_CSV_GetCellAsString(csv, 0, 0, truncated, 2);
    require(truncated[0] == '4' && truncated[1] == '2',
        "truncated string copy mismatch");
    char binary[3]{};
    DLL_CSV_GetCellAsBinary(csv, 0, 0, binary, 3);
    require(binary[0] == '4' && binary[1] == '2' && binary[2] == '\0',
        "binary copy mismatch");

    // Native 0x1800679f0 returns without touching the parser when the file object is not valid.
    CM3D2_FileHandle invalid_file = nullptr;
    DLL_FileMemory_CreateileMemory(&invalid_file);
    DLL_CSV_Open(csv, DLL_FileMemory_GetInterfaceFilePointer(&invalid_file));
    require(DLL_CSV_IsValid(csv) == 1 && DLL_CSV_GetCellAsInteger(csv, 0, 0) == 42,
        "an invalid file object must leave the previous table untouched");
    DLL_FileMemory_DeleteileMemory(&invalid_file);

    // Native always decrypts: an already decoded table is not accepted and the table state is reset.
    DLL_FileMemory_SetData(&file, bytes.data(), static_cast<std::int32_t>(bytes.size()));
    DLL_CSV_Open(csv, DLL_FileMemory_GetInterfaceFilePointer(&file));
    require(DLL_CSV_IsValid(csv) == 0 && DLL_CSV_GetMaxCellX(csv) == 0 && DLL_CSV_GetCellAsInteger(csv, 0, 0) == 0,
        "a plain decoded table must be rejected and reset the parser");
    char untouched[2] = {'x', 'x'};
    DLL_CSV_GetCellAsString(csv, 0, 0, untouched, 2);
    require(untouched[0] == 'x', "an invalid table must not touch the string destination");

    DLL_CSV_DeleteCsvParser(csv);
    DLL_FileMemory_DeleteileMemory(&file);
    return 0;
}
