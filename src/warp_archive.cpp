#include "warp_archive.h"

#include "file_system_windows.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

namespace cm3d2::internal {

namespace {

constexpr std::uint32_t kWarpMagic = 0x70726177;               // "warp"
constexpr std::uint64_t kWarcMagic = 0xf145aaff63726177ULL;    // "warc" ff aa 45 f1
constexpr std::size_t kHeadSize = 0x800;

// The loader's own key for the index carrier (words stored at local_318, written little endian).
constexpr std::array<std::uint32_t, 4> kIndexKeyWords{0xecb97957U, 0x9f48d853U, 0xc50013a9U, 0x9656b303U};

std::uint32_t readU32(const unsigned char* at) {
    std::uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

std::array<unsigned char, 16> wordsToKey(const std::array<std::uint32_t, 4>& words) {
    std::array<unsigned char, 16> key{};
    for (std::size_t i = 0; i < 4; ++i) {
        std::memcpy(key.data() + i * 4, &words[i], 4);
    }
    return key;
}

// FUN_180075190 (also used by the warc crypt mode 2): the 16-byte IV stream the carrier derives from its trailer seed.
std::array<unsigned char, 16> xshift16(std::uint32_t seed) {
    std::uint32_t state[4] = {0x075bcd15U, 0x159a55e5U, 0x1f123bb5U, seed};
    std::array<unsigned char, 16> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint32_t t = (state[0] << 11) ^ state[0];
        const std::uint32_t previous = state[3];
        const std::uint32_t next = (((previous >> 11) ^ t) >> 8) ^ previous ^ t;
        state[0] = state[1];
        state[1] = state[2];
        state[2] = previous;
        state[3] = next;
        std::memcpy(out.data() + i * 4, &next, 4);
    }
    return out;
}

// FUN_18010a140: carrier = AES-128-CBC ciphertext + 5 byte trailer { pad ^ pad2, seed ^ 0xbfbfbfbf }; the IV is
// xshift16(seed), the plaintext is the ciphertext minus `pad` bytes. The length must be 5 modulo 16.
bool decodeCarrier(const std::array<unsigned char, 16>& key, const unsigned char* in, std::size_t length,
    std::vector<unsigned char>& out) {
    out.clear();
    if (length < 5 || ((length + 0xfb) & 0xf) != 0) {
        return false;
    }
    const std::size_t cipher = length - 5;
    const std::size_t pad = in[cipher] ^ in[cipher + 1];
    if (pad >= 16 || pad > cipher) {
        return false;
    }
    auto iv = xshift16(readU32(in + cipher + 1) ^ 0xbfbfbfbfU);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)), 0) >= 0 &&
            BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, const_cast<PUCHAR>(key.data()),
                static_cast<ULONG>(key.size()), 0) >= 0) {
            std::vector<unsigned char> plain(cipher);
            ULONG written = 0;
            if (cipher == 0 ||
                (BCryptDecrypt(handle, const_cast<PUCHAR>(in), static_cast<ULONG>(cipher), nullptr, iv.data(),
                     static_cast<ULONG>(iv.size()), plain.data(), static_cast<ULONG>(plain.size()), &written, 0) >= 0 &&
                    written == cipher)) {
                plain.resize(cipher - pad);
                out = std::move(plain);
                ok = true;
            }
        }
    }
    if (handle != nullptr) BCryptDestroyKey(handle);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

// The standard reflected CRC-32 (the table at 0x1801cdb30 starts 0, 0x77073096, 0xee0e612c).
std::uint32_t crc32(const unsigned char* data, std::size_t size) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320U ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xff];
    }
    return ~crc;
}

// The key of blob 2 from the CRC of the base archive's head (the loader's shift/xor chain after bswap).
std::array<unsigned char, 16> blobKey(std::uint32_t crc) {
    const std::uint32_t x = (crc << 24) | ((crc & 0xff00U) << 8) | ((crc >> 8) & 0xff00U) | (crc >> 24);
    std::array<std::uint32_t, 4> words{};
    words[0] = (x >> 0x13) ^ x ^ 0xd9ea5670U;
    words[1] = (words[0] >> 0x13) ^ words[0] ^ 0xc7f24898U;
    words[2] = (words[1] >> 0x13) ^ words[1] ^ 0x8e415c26U;
    const std::uint32_t y = (x << 0xb) ^ x;
    words[3] = (((words[2] >> 0xb) ^ y) >> 8) ^ y ^ words[2];
    return wordsToKey(words);
}

std::string wideToCp932(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(932, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        WideCharToMultiByte(932, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    }
    return out;
}

std::wstring cp932ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(932, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(932, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    }
    return out;
}

// FUN_18008ab30 / FUN_18010acb0: the directory of the patch is the narrow (CP932) raw name with every occurrence of its
// last component removed. The component starts after the last backslash, else after the last slash, else is the whole name.
std::string patchDirectory(const std::wstring& raw_name) {
    std::string narrow = wideToCp932(raw_name);
    std::string component = narrow;
    for (const char separator : {'\\', '/'}) {
        const auto at = narrow.rfind(separator);
        if (at != std::string::npos) {
            component = narrow.substr(at + 1);
            break;
        }
    }
    if (!narrow.empty() && !component.empty()) {
        for (auto at = narrow.find(component); at != std::string::npos; at = narrow.find(component)) {
            narrow.erase(at, component.size());
        }
    }
    return narrow;
}

std::wstring lowered(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

bool readAt(std::ifstream& file, std::uint64_t offset, void* out, std::size_t size) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(file.gcount()) == size;
}

} // namespace

WarpOutcome convertWarpArchive(const std::wstring& raw_name, const std::wstring& full_path, const WarpContext& context) {
    std::ifstream file(full_path, std::ios::binary);
    if (!file) {
        return WarpOutcome::NotWarp;
    }
    file.seekg(0, std::ios::end);
    const std::uint64_t file_size = static_cast<std::uint64_t>(file.tellg());
    unsigned char magic[4] = {};
    if (file_size < 4 || !readAt(file, 0, magic, 4) || readU32(magic) != kWarpMagic) {
        return WarpOutcome::NotWarp;
    }

    // Index: the name of the base archive.
    unsigned char length_bytes[4] = {};
    if (!readAt(file, 8, length_bytes, 4)) {
        return WarpOutcome::Failed;
    }
    const std::uint32_t index_length = readU32(length_bytes);
    if (index_length > file_size) {
        return WarpOutcome::Failed;
    }
    std::vector<unsigned char> index(index_length);
    if (index_length != 0 && !readAt(file, 12, index.data(), index_length)) {
        return WarpOutcome::Failed;
    }
    std::vector<unsigned char> index_plain;
    if (index_length == 0 || !decodeCarrier(wordsToKey(kIndexKeyWords), index.data(), index.size(), index_plain)) {
        return WarpOutcome::Failed;
    }
    const auto name_end = std::find(index_plain.begin(), index_plain.end(), static_cast<unsigned char>(0));
    const std::wstring base_name = lowered(cp932ToWide(std::string(index_plain.begin(), name_end)));

    // Where is the base archive? The preferred directories first, then the patch's own directory below the base directory.
    std::vector<std::wstring> candidates = context.preferred_directories;
    candidates.push_back(context.base_directory + cp932ToWide(patchDirectory(raw_name)));
    std::array<unsigned char, kHeadSize> head;
    bool found = false;
    for (auto candidate : candidates) {
        if (candidate.empty() || candidate.back() != L'\\') {
            candidate.push_back(L'\\');
        }
        const std::uint64_t id = fileSystemNameHash(lowered(candidate + base_name));
        head.fill(0xff);
        if (context.read_mounted_head && context.read_mounted_head(id, head.data(), head.size())) {
            found = true;
            break;
        }
    }
    if (!found) {
        return WarpOutcome::Failed;
    }
    const auto key = blobKey(crc32(head.data(), head.size()));

    // Blob 2 and the remaining body.
    const std::uint64_t blob_length_at = 12 + static_cast<std::uint64_t>(index_length) + 8;
    unsigned char blob_length_bytes[4] = {};
    if (!readAt(file, blob_length_at, blob_length_bytes, 4)) {
        return WarpOutcome::Failed;
    }
    const std::uint32_t blob_length = readU32(blob_length_bytes);
    const std::uint64_t blob_at = blob_length_at + 4;
    if (blob_at + blob_length > file_size) {
        return WarpOutcome::Failed;
    }
    std::vector<unsigned char> blob(blob_length);
    if (blob_length != 0 && !readAt(file, blob_at, blob.data(), blob_length)) {
        return WarpOutcome::Failed;
    }
    std::vector<unsigned char> plain;
    decodeCarrier(key, blob.data(), blob.size(), plain);  // a failed decode writes nothing, as in the original

    const std::wstring temporary = full_path + L"_tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(reinterpret_cast<const char*>(plain.data()), static_cast<std::streamsize>(plain.size()));
            std::vector<unsigned char> chunk(0x1000);
            std::uint64_t at = blob_at + blob_length;
            while (at < file_size) {
                const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), file_size - at));
                if (!readAt(file, at, chunk.data(), count)) break;
                out.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(count));
                at += count;
            }
        }
    }
    file.close();

    // The converted file must start with the warc magic.
    std::uint64_t converted_magic = 0;
    {
        std::ifstream check(temporary, std::ios::binary);
        if (!check || !readAt(check, 0, &converted_magic, 8) || converted_magic != kWarcMagic) {
            check.close();
            DeleteFileW(temporary.c_str());
            return WarpOutcome::Failed;
        }
    }
    if (!DeleteFileW(full_path.c_str())) {
        return WarpOutcome::Failed;  // the temporary file stays, as in the original
    }
    if (!MoveFileW(temporary.c_str(), full_path.c_str())) {
        return WarpOutcome::Failed;
    }
    return WarpOutcome::Converted;
}

} // namespace cm3d2::internal
