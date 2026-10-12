#include "cm3d2/csv_api.h"
#include "file_api_internal.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

constexpr std::uint32_t kDecodedCsvMagic = 0xff767377U;
/* Copied by the DLL's startup initializer from its read-only image constant. */
constexpr std::array<std::byte, 16> kCsvAesKey{
    std::byte{0xaa}, std::byte{0xc9}, std::byte{0xd2}, std::byte{0x35},
    std::byte{0x22}, std::byte{0x87}, std::byte{0x20}, std::byte{0xf2},
    std::byte{0x40}, std::byte{0xc5}, std::byte{0x61}, std::byte{0x7c},
    std::byte{0x01}, std::byte{0xdf}, std::byte{0x66}, std::byte{0x54}
};

struct Cell {
    std::uint32_t offset = 0;
    std::uint32_t stored_size = 0;
};

struct CsvParserState {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::size_t blob_offset = 0;
    std::vector<Cell> cells;
};

/* Matches the observed outer allocation: parser, decoded buffer, byte count. */
struct CsvHandleLayout {
    CsvParserState* parser = nullptr;
    std::byte* decoded = nullptr;
    std::uint64_t decoded_size = 0;
};

static_assert(sizeof(CsvHandleLayout) == 0x18);

CsvHandleLayout* handle(CM3D2_CsvHandle csv) {
    return static_cast<CsvHandleLayout*>(csv);
}

const CsvHandleLayout* handle(const void* csv) {
    return static_cast<const CsvHandleLayout*>(csv);
}

std::uint32_t readU32(const std::byte* source) {
    std::uint32_t value = 0;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

std::array<std::byte, 16> deriveIv(std::uint32_t seed) {
    std::array<std::uint32_t, 4> state{
        0x075bcd15U, 0x159a55e5U, 0x1f123bb5U, seed
    };
    std::array<std::byte, 16> iv{};
    for (std::size_t index = 0; index < 4; ++index) {
        const auto t = (state[0] << 11) ^ state[0];
        const auto previous = state[3];
        const auto next = (((previous >> 11) ^ t) >> 8) ^ previous ^ t;
        state[0] = state[1];
        state[1] = state[2];
        state[2] = previous;
        state[3] = next;
        std::memcpy(iv.data() + index * sizeof(next), &next, sizeof(next));
    }
    return iv;
}

bool decryptCarrier(
    const std::byte* carrier,
    std::uint64_t carrier_size,
    std::byte*& plaintext,
    std::uint64_t& plaintext_size) {
    plaintext = nullptr;
    plaintext_size = 0;
    if (carrier == nullptr || carrier_size < 5 ||
        carrier_size - 5 > std::numeric_limits<ULONG>::max()) {
        return false;
    }

    const auto cipher_size = carrier_size - 5;
    if (cipher_size == 0 || (cipher_size & 0xfU) != 0) {
        return false;
    }

    const auto pad_count = std::to_integer<std::uint8_t>(carrier[cipher_size]) ^
        std::to_integer<std::uint8_t>(carrier[cipher_size + 1]);
    if (pad_count >= 16 || pad_count > cipher_size) {
        return false;
    }
    const auto encoded_seed = readU32(carrier + cipher_size + 1);
    const auto seed = encoded_seed ^ 0xbfbfbfbfU;
    auto iv = deriveIv(seed);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<UCHAR> key_object;
    bool success = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) {
        return false;
    }
    const auto mode_status = BCryptSetProperty(
        algorithm,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
        static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)),
        0);
    DWORD object_size = 0;
    DWORD result_size = 0;
    if (mode_status >= 0 &&
        BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size),
            &result_size,
            0) >= 0) {
        key_object.resize(object_size);
        if (BCryptGenerateSymmetricKey(
                algorithm,
                &key,
                key_object.data(),
                object_size,
                reinterpret_cast<PUCHAR>(const_cast<std::byte*>(kCsvAesKey.data())),
                static_cast<ULONG>(kCsvAesKey.size()),
                0) >= 0) {
            auto* output = new (std::nothrow) std::byte[static_cast<std::size_t>(cipher_size)];
            if (output != nullptr) {
                ULONG written = 0;
                auto iv_copy = iv;
                const auto status = BCryptDecrypt(
                    key,
                    reinterpret_cast<PUCHAR>(const_cast<std::byte*>(carrier)),
                    static_cast<ULONG>(cipher_size),
                    nullptr,
                    reinterpret_cast<PUCHAR>(iv_copy.data()),
                    static_cast<ULONG>(iv_copy.size()),
                    reinterpret_cast<PUCHAR>(output),
                    static_cast<ULONG>(cipher_size),
                    &written,
                    0);
                if (status >= 0 && written == cipher_size) {
                    plaintext = output;
                    plaintext_size = cipher_size - pad_count;
                    success = true;
                } else {
                    delete[] output;
                }
            }
        }
    }

    if (key != nullptr) {
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}

void reset(CsvHandleLayout& csv) {
    delete[] csv.decoded;
    csv.decoded = nullptr;
    csv.decoded_size = 0;
    if (csv.parser != nullptr) {
        *csv.parser = CsvParserState{};
    }
}

bool parseDecoded(CsvHandleLayout& csv, std::byte* bytes, std::uint64_t size) {
    constexpr std::uint64_t header_size = 3 * sizeof(std::uint32_t);
    if (bytes == nullptr || size < header_size || readU32(bytes) != kDecodedCsvMagic) {
        return false;
    }

    const auto width = static_cast<std::int32_t>(readU32(bytes + 4));
    const auto height = static_cast<std::int32_t>(readU32(bytes + 8));
    if (width < 0 || height < 0) {
        return false;
    }

    const auto cell_count = static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height);
    if (cell_count > (std::numeric_limits<std::uint64_t>::max() - header_size) / 8) {
        return false;
    }
    const auto blob_offset = header_size + cell_count * 8;
    if (blob_offset > size || cell_count > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    std::vector<Cell> cells;
    cells.reserve(static_cast<std::size_t>(cell_count));
    const auto blob_size = size - blob_offset;
    for (std::uint64_t index = 0; index < cell_count; ++index) {
        const auto descriptor = bytes + header_size + index * 8;
        const Cell cell{readU32(descriptor), readU32(descriptor + 4)};
        if (cell.offset > blob_size || cell.stored_size > blob_size - cell.offset) {
            return false;
        }
        cells.push_back(cell);
    }

    csv.decoded = bytes;
    csv.decoded_size = size;
    csv.parser->width = width;
    csv.parser->height = height;
    csv.parser->blob_offset = static_cast<std::size_t>(blob_offset);
    csv.parser->cells = std::move(cells);
    return true;
}

const Cell* cell(const CsvHandleLayout* csv, std::int32_t x, std::int32_t y) {
    if (csv == nullptr || csv->parser == nullptr || csv->decoded == nullptr ||
        x < 0 || y < 0 || x >= csv->parser->width || y >= csv->parser->height) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(y) *
        static_cast<std::size_t>(csv->parser->width) + static_cast<std::size_t>(x);
    const auto& result = csv->parser->cells[index];
    return result.stored_size == 0 ? nullptr : &result;
}

const char* cellData(const CsvHandleLayout& csv, const Cell& item) {
    return reinterpret_cast<const char*>(
        csv.decoded + csv.parser->blob_offset + item.offset);
}

} // namespace

extern "C" {

CM3D2_CsvHandle CM3D2_CALL DLL_CSV_CreateCsvParser(void) {
    auto* csv = new CsvHandleLayout{};
    csv->parser = new CsvParserState{};
    return csv;
}

void CM3D2_CALL DLL_CSV_DeleteCsvParser(CM3D2_CsvHandle csv_handle) {
    auto* csv = handle(csv_handle);
    if (csv == nullptr) {
        return;
    }
    delete[] csv->decoded;
    delete csv->parser;
    delete csv;
}

// The managed declaration returns Boolean. The original returns 0 for an invalid file and otherwise the result of the table loader
// (FUN_18010a780: 1, or 0 when the decoded bytes do not start with the table magic).
uint32_t CM3D2_CALL DLL_CSV_Open(CM3D2_CsvHandle csv_handle, CM3D2_FileHandle file) {
    auto* csv = handle(csv_handle);
    if (csv == nullptr || csv->parser == nullptr) {
        return 0;
    }
    CM3D2_FileHandle local_file = file;
    if (!DLL_File_IsValid(&local_file)) {
        return 0;  // native (0x1800679f0) returns here without touching the parser or the decoded buffer
    }
    reset(*csv);
    const auto size = DLL_File_GetSize(&local_file);
    if (size == 0 || size > std::numeric_limits<std::size_t>::max()) {
        return 0;
    }

    auto* decoded = new (std::nothrow) std::byte[static_cast<std::size_t>(size)];
    if (decoded == nullptr) {
        return 0;
    }
    const auto* direct = cm3d2::internal::fileData(file);
    if (direct != nullptr) {
        std::memcpy(decoded, direct, static_cast<std::size_t>(size));
    } else if (DLL_File_Read(&local_file, decoded, size) != size) {
        delete[] decoded;
        return 0;
    }

    // Native always runs the carrier decryption (FUN_18010a140); there is no path for already decoded tables.
    std::byte* plaintext = nullptr;
    std::uint64_t plaintext_size = 0;
    if (!decryptCarrier(decoded, size, plaintext, plaintext_size)) {
        delete[] decoded;
        return 0;
    }
    delete[] decoded;
    if (!parseDecoded(*csv, plaintext, plaintext_size)) {
        delete[] plaintext;
        return 0;
    }
    return 1;
}

void CM3D2_CALL DLL_CSV_GetCellAsBinary(
    CM3D2_CsvHandle csv_handle,
    int32_t x,
    int32_t y,
    void* destination,
    int32_t capacity) {
    const auto* csv = handle(csv_handle);
    const auto* item = cell(csv, x, y);
    if (item == nullptr || destination == nullptr || capacity <= 0) {
        return;
    }
    const auto payload_size = item->stored_size - 1;
    const auto count = std::min<std::uint32_t>(
        payload_size, static_cast<std::uint32_t>(capacity));
    std::memcpy(destination, cellData(*csv, *item), count);
}

void CM3D2_CALL DLL_CSV_GetCellAsString(
    CM3D2_CsvHandle csv_handle,
    int32_t x,
    int32_t y,
    char* destination,
    int32_t capacity) {
    if (destination == nullptr || capacity <= 0) {
        return;
    }
    const auto* csv = handle(csv_handle);
    // The export returns before touching the buffer when the table is not valid (height <= 0).
    if (csv == nullptr || csv->parser == nullptr || csv->parser->height <= 0) {
        return;
    }
    destination[0] = '\0';
    const auto* item = cell(csv, x, y);
    if (item == nullptr) {
        return;
    }
    const auto count = std::min<std::uint32_t>(
        item->stored_size, static_cast<std::uint32_t>(capacity));
    std::memcpy(destination, cellData(*csv, *item), count);
}

uint32_t CM3D2_CALL DLL_CSV_GetCellBool(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y) {
    return DLL_CSV_GetCellAsInteger(csv, x, y) != 0;
}

int32_t CM3D2_CALL DLL_CSV_GetCellAsInteger(
    CM3D2_CsvHandle csv_handle,
    int32_t x,
    int32_t y) {
    const auto* csv = handle(csv_handle);
    const auto* item = cell(csv, x, y);
    return item == nullptr ? 0 : std::strtol(cellData(*csv, *item), nullptr, 10);
}

float CM3D2_CALL DLL_CSV_GetCellAsReal(
    CM3D2_CsvHandle csv_handle,
    int32_t x,
    int32_t y) {
    const auto* csv = handle(csv_handle);
    const auto* item = cell(csv, x, y);
    return item == nullptr ? 0.0F : static_cast<float>(std::atof(cellData(*csv, *item)));
}

int32_t CM3D2_CALL DLL_CSV_GetDataSizeString(
    CM3D2_CsvHandle csv_handle,
    int32_t x,
    int32_t y) {
    const auto* item = cell(handle(csv_handle), x, y);
    return item == nullptr ? 0 : static_cast<std::int32_t>(item->stored_size);
}

int32_t CM3D2_CALL DLL_CSV_GetDataSizeBinary(
    CM3D2_CsvHandle csv_handle,
    int32_t x,
    int32_t y) {
    const auto* item = cell(handle(csv_handle), x, y);
    return item == nullptr ? 0 : static_cast<std::int32_t>(item->stored_size - 1);
}

uint32_t CM3D2_CALL DLL_CSV_IsCellToExistData(
    CM3D2_CsvHandle csv_handle,
    int32_t x,
    int32_t y) {
    return cell(handle(csv_handle), x, y) != nullptr;
}

uint32_t CM3D2_CALL DLL_CSV_IsValid(CM3D2_CsvHandle csv_handle) {
    const auto* csv = handle(csv_handle);
    return csv != nullptr && csv->parser != nullptr && csv->parser->height > 0;
}

/* Native DLL_CSV_GetMaxCellX (0x180067ef0) and ...Y (0x180067f10) return 0 when the
 * decoded size is zero, otherwise the parser's width/height counts (not max indices). */
int32_t CM3D2_CALL DLL_CSV_GetMaxCellX(CM3D2_CsvHandle csv_handle) {
    const auto* csv = handle(csv_handle);
    return csv == nullptr || csv->decoded_size == 0 || csv->parser == nullptr ? 0 : csv->parser->width;
}

int32_t CM3D2_CALL DLL_CSV_GetMaxCellY(CM3D2_CsvHandle csv_handle) {
    const auto* csv = handle(csv_handle);
    return csv == nullptr || csv->decoded_size == 0 || csv->parser == nullptr ? 0 : csv->parser->height;
}

} // extern "C"
