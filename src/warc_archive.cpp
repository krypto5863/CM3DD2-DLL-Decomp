#include "warc_archive.h"
#include "msvc_utf8_facet.h"

#include "cm3d2/file_api.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <climits>

extern "C" {
// stb_image's fixed-output zlib decoder, compiled as C in stb_image_impl.c.
int stbi_zlib_decode_buffer(char* obuffer, int olen, const char* ibuffer, int ilen);
}

namespace cm3d2::internal {
struct WarcArchive::MountedStream {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~MountedStream() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
namespace {

constexpr unsigned char kMagic[8] = {0x77, 0x61, 0x72, 0x63, 0xff, 0xaa, 0x45, 0xf1};
constexpr std::uint32_t kMinVersion = 1000;  // native: version > 999
constexpr std::uint32_t kTagRecords = 0;
constexpr std::uint32_t kTagParallel = 1;
constexpr std::uint32_t kTagData = 2;
constexpr std::uint32_t kTagNames = 3;
constexpr std::size_t kTableHeaderMin = 0x18;
constexpr std::size_t kRecordSize = 16;
constexpr int kMaxDirectoryDepth = 64;
// FUN_18008feb0 serializes mounted-stream member reads under one global mutex.
std::mutex g_mounted_archive_reads;

struct RestoreFilePosition {
    HANDLE file;
    LARGE_INTEGER saved{};
    bool valid;
    explicit RestoreFilePosition(HANDLE input) : file(input), valid(SetFilePointerEx(input, LARGE_INTEGER{}, &saved, FILE_CURRENT) != 0) {}
    ~RestoreFilePosition() { if (valid) SetFilePointerEx(file, saved, nullptr, FILE_BEGIN); }
};

std::uint32_t readU32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t readU64(const unsigned char* p) {
    return static_cast<std::uint64_t>(readU32(p)) | (static_cast<std::uint64_t>(readU32(p + 4)) << 32);
}

bool readAt(HANDLE file, std::uint64_t offset, void* destination, std::size_t count) {
    if (count > MAXDWORD) return false;
    LARGE_INTEGER target{}; target.QuadPart = static_cast<LONGLONG>(offset);
    DWORD completed = 0;
    return SetFilePointerEx(file, target, nullptr, FILE_BEGIN) != 0 &&
        ReadFile(file, destination, static_cast<DWORD>(count), &completed, nullptr) != 0 && completed == count;
}

// Native lowercasing touches only ASCII A-Z (full-width capitals are left alone).
wchar_t lowerAscii(wchar_t ch) {
    return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
}

// FUN_18008ee40: lowercase, split on '\' (preferred) or '/', skip empty interior parts.
std::vector<std::wstring> splitComponents(std::wstring_view name) {
    std::wstring text(name);
    for (auto& ch : text) {
        ch = lowerAscii(ch);
    }
    std::vector<std::wstring> parts;
    std::size_t at = 0;
    for (;;) {
        std::size_t separator = text.find(L'\\', at);
        if (separator == std::wstring::npos) {
            separator = text.find(L'/', at);
        }
        if (separator == std::wstring::npos) {
            parts.push_back(text.substr(at));
            break;
        }
        if (separator != at) {
            parts.push_back(text.substr(at, separator - at));
        }
        at = separator + 1;
    }
    return parts;
}

// The 16-byte key stream the native derives from a seed (FUN_180075190).
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

// Crypt mode 2 (FUN_18010a140): AES-128-CBC. Key = xshift16(decoded size); IV =
// xshift16(trailer seed). The last 5 bytes are { pad ^ pad2, seed ^ 0xbfbfbfbf }.
bool carrierSize(const std::vector<unsigned char>& in, std::size_t& decoded) {
    const std::size_t length = in.size();
    if (length < 5 || ((length + 0xfb) & 0xf) != 0) {
        return false;
    }
    const std::size_t cipher = length - 5;
    const std::size_t pad = in[cipher] ^ in[cipher + 1];
    if (pad >= 16 || pad > cipher) {
        return false;
    }
    decoded = cipher - pad;
    return true;
}

// 18010a140 copies complete blocks to the destination, then (when padded)
// decrypts the last block into a 16-byte scratch area and copies only its tail.
// Avoid a full padded plaintext vector when the destination is smaller than
// the cipher by 1..15 bytes. CNG remains an AES implementation substitution.
bool decryptCarrier(const std::vector<unsigned char>& in, std::uint32_t key_seed,
    unsigned char* out, std::size_t capacity, std::size_t& produced) {
    if (!carrierSize(in, produced) || produced > capacity) return false;
    const auto cipher = in.size() - 5;
    const auto pad = cipher - produced;
    if (cipher == 0) return true;
    const auto key = xshift16(key_seed);
    auto iv = xshift16(readU32(in.data() + in.size() - 4) ^ 0xbfbfbfbfU);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)), 0) >= 0 &&
            BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0,
                const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) >= 0) {
            ULONG written = 0;
            const auto prefix = pad == 0 ? cipher : cipher - 16;
            bool prefix_ok = prefix == 0;
            if (prefix != 0) {
                prefix_ok = BCryptDecrypt(handle, const_cast<PUCHAR>(in.data()), static_cast<ULONG>(prefix), nullptr,
                    iv.data(), static_cast<ULONG>(iv.size()), out, static_cast<ULONG>(prefix),
                    &written, 0) >= 0 && written == prefix;
            }
            if (prefix_ok && pad == 0) {
                ok = true;
            } else if (prefix_ok) {
                // Set the last block's CBC IV explicitly; do not depend on CNG's
                // handling of the mutable IV buffer after the prefix operation.
                if (prefix != 0) std::memcpy(iv.data(), in.data() + prefix - 16, 16);
                std::array<unsigned char, 16> tail{};
                written = 0;
                if (BCryptDecrypt(handle, const_cast<PUCHAR>(in.data()+prefix), 16, nullptr,
                    iv.data(), 16, tail.data(), 16, &written, 0) >= 0 && written == 16) {
                    std::memcpy(out+prefix, tail.data(), 16-pad);
                    ok = true;
                }
            }
        }
    }
    if (handle != nullptr) BCryptDestroyKey(handle);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

} // namespace

std::uint64_t archiveNameKey(std::wstring_view component) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (wchar_t ch : component) {
        const auto c = static_cast<std::uint32_t>(ch);
        h = (h ^ (c & 0xff)) * 0x100000001b3ULL;
        h = (h ^ ((c >> 8) & 0xff)) * 0x100000001b3ULL;
    }
    return h ^ (h >> 32);
}

// Tag 1 keys use the same FNV-1a fold over the UTF-8 bytes of the name. The bytes come from the recovered facet's to_bytes (FUN_18006c3f0:
// 1/2/3 bytes per 16-bit unit, surrogate halves encoded individually, never a failure), not from Win32 CP_UTF8. The converted range ends at the
// first NUL unit: the callers (e.g. FUN_18008bf90) scan the normalised wide name for its terminator and pass [begin, that NUL) to to_bytes.
std::uint64_t archiveNameKeyUtf8(std::wstring_view component) {
    const std::string bytes = msvc_utf8::toUtf8(std::wstring(component.substr(0, component.find(L'\0'))));
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : bytes) {
        h = (h ^ c) * 0x100000001b3ULL;
    }
    return h ^ (h >> 32);
}

bool WarcArchive::open(const std::wstring& path, WarcArchive& out) {
    // 18008ab30 constructs Win32File, NOT FileNormalWindows; 180074520
    // opens GENERIC_READ / FILE_SHARE_READ / OPEN_EXISTING / READONLY.
    auto stream = std::make_shared<MountedStream>();
    stream->handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);
    if (stream->handle == INVALID_HANDLE_VALUE) return false;
    HANDLE file = stream->handle;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 16) return false;
    const std::uint64_t size = static_cast<std::uint64_t>(length.QuadPart);
    unsigned char header[16] = {};
    if (!file || !readAt(file, 0, header, sizeof(header)) ||
        std::memcmp(header, kMagic, sizeof(kMagic)) != 0 || readU32(header + 8) < kMinVersion) {
        return false;
    }

    // Walk the section chain; a repeated tag replaces the earlier one, as the native map does.
    std::map<std::uint32_t, Section> sections;
    const std::uint32_t count = readU32(header + 12);
    std::uint64_t at = 16;
    for (std::uint32_t i = 0; i < count; ++i) {
        unsigned char entry[12];
        if (at + sizeof(entry) > size || !readAt(file, at, entry, sizeof(entry))) {
            return false;
        }
        Section section;
        section.payload = at + sizeof(entry);
        section.size = readU64(entry + 4);
        if (section.size > size - section.payload) {
            return false;
        }
        sections[readU32(entry)] = section;
        at = section.payload + section.size;
    }

    WarcArchive result;
    result.path_ = path;
    result.stream_ = std::move(stream);
    result.file_size_ = size;
    if (count == 0) {
        out = std::move(result);  // the native reports success without mounting anything
        return true;
    }
    if (!sections.count(kTagRecords) || !sections.count(kTagParallel) || !sections.count(kTagData)) {
        return false;
    }
    result.data_ = sections[kTagData];
    if (sections.count(kTagNames)) {
        result.names_section_ = sections[kTagNames];
        result.has_names_ = true;
    }

    const Section* sources[2] = {&sections[kTagRecords], &sections[kTagParallel]};
    for (int which = 0; which < 2; ++which) {
        const Section& source = *sources[which];
        if (source.size < kTableHeaderMin || source.size > 256ULL * 1024 * 1024) {
            return false;
        }
        result.tables_[which].resize(static_cast<std::size_t>(source.size));
        if (!readAt(file, source.payload, result.tables_[which].data(), result.tables_[which].size()) ||
            !result.tableValid(which, 0)) {
            return false;
        }
    }
    out = std::move(result);
    return true;
}

// A table is valid when its header and both record arrays lie inside the buffer.
bool WarcArchive::tableValid(int which, std::size_t table) const {
    const auto& table_ = tables_[which];
    if (table > table_.size() || table_.size() - table < kTableHeaderMin) {
        return false;
    }
    const unsigned char* base = table_.data() + table;
    const std::uint64_t header = readU32(base);
    const std::uint64_t dirs = readU32(base + 0x10);
    const std::uint64_t files = readU32(base + 0x14);
    const std::uint64_t end = header + (dirs + files) * kRecordSize;
    return header >= kTableHeaderMin && end <= table_.size() - table;
}

bool WarcArchive::lookupDirectory(int which, std::size_t table, std::uint64_t key, std::size_t& nested) const {
    const auto& table_ = tables_[which];
    if (!tableValid(which, table)) {
        return false;
    }
    const unsigned char* base = table_.data() + table;
    const std::uint64_t header = readU32(base);
    const std::uint64_t dirs = readU32(base + 0x10);
    std::uint64_t low = 0;
    std::uint64_t high = dirs;
    while (low < high) {
        const std::uint64_t mid = (low + high) / 2;
        if (readU64(base + header + mid * kRecordSize) < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low == dirs || readU64(base + header + low * kRecordSize) != key) {
        return false;
    }
    const std::uint64_t relative = readU64(base + header + low * kRecordSize + 8);
    if (relative > table_.size() - table) {
        return false;
    }
    nested = table + static_cast<std::size_t>(relative);
    return tableValid(which, nested);
}

// Files are searched with a lower bound on the sorted keys. When `recurse` is set and
// the key is missing, every directory is searched in turn (FUN_180089b30).
bool WarcArchive::lookupFile(int which, std::size_t table, std::uint64_t key, std::uint64_t& value, bool recurse) const {
    const auto& table_ = tables_[which];
    static thread_local int depth = 0;
    if (depth >= kMaxDirectoryDepth || !tableValid(which, table)) {
        return false;
    }
    const unsigned char* base = table_.data() + table;
    const std::uint64_t header = readU32(base);
    const std::uint64_t dirs = readU32(base + 0x10);
    const std::uint64_t files = readU32(base + 0x14);
    const unsigned char* records = base + header + dirs * kRecordSize;
    std::uint64_t low = 0;
    std::uint64_t high = files;
    while (low < high) {
        const std::uint64_t mid = (low + high) / 2;
        if (readU64(records + mid * kRecordSize) < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low < files && readU64(records + low * kRecordSize) == key) {
        value = readU64(records + low * kRecordSize + 8);
        return true;
    }
    if (!recurse) {
        return false;
    }
    ++depth;
    bool found = false;
    for (std::uint64_t i = 0; i < dirs && !found; ++i) {
        const std::uint64_t relative = readU64(base + header + i * kRecordSize + 8);
        if (relative <= table_.size() - table && tableValid(which, table + static_cast<std::size_t>(relative))) {
            found = lookupFile(which, table + static_cast<std::size_t>(relative), key, value, true);
        }
    }
    --depth;
    return found;
}

bool WarcArchive::find(std::wstring_view name, std::uint64_t& value, bool utf8_keys) const {
    // The native matches "name " to "name"; trailing spaces are dropped before splitting.
    while (!name.empty() && name.back() == L' ') {
        name.remove_suffix(1);
    }
    if (name.empty() || tables_[0].empty()) {
        return false;
    }
    const auto parts = splitComponents(name);
    if (parts.empty()) {
        return false;
    }
    const int which = utf8_keys ? 1 : 0;
    const auto key = [&](const std::wstring& part) {
        return utf8_keys ? archiveNameKeyUtf8(part) : archiveNameKey(part);
    };
    std::size_t table = 0;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!lookupDirectory(which, table, key(parts[i]), table)) {
            return false;
        }
    }
    return lookupFile(which, table, key(parts.back()), value, parts.size() == 1);
}

// A section or member is { compress, crypt, decodedSize, payloadSize } + payload
// (FUN_180089760 / FUN_180089800).
bool WarcArchive::readSection(const Section& section, std::vector<unsigned char>& out) const {
    std::unique_ptr<std::byte[]> bytes;
    std::uint32_t size = 0;
    if (!readSectionOwned(section, bytes, size)) return false;
    const auto* first = reinterpret_cast<const unsigned char*>(bytes.get());
    out.assign(first, first + size);
    return true;
}

bool WarcArchive::readSectionOwned(const Section& section, std::unique_ptr<std::byte[]>& out, std::uint32_t& size) const {
    if (!stream_) return false;
    std::lock_guard<std::mutex> lock(g_mounted_archive_reads);
    HANDLE file = stream_->handle;
    RestoreFilePosition restore(file);
    unsigned char header[16];
    if (!restore.valid || section.size < sizeof(header) || !readAt(file, section.payload, header, sizeof(header))) {
        return false;
    }
    const std::uint32_t compress = readU32(header);
    const std::uint32_t crypt = readU32(header + 4);
    const std::uint32_t decoded = readU32(header + 8);
    const std::uint32_t length = readU32(header + 12);
    if (length > section.size - sizeof(header)) {
        return false;
    }
    auto result = std::make_unique<std::byte[]>(decoded);
    std::vector<unsigned char> data(length);
    if (length != 0 && !readAt(file, section.payload + sizeof(header), data.data(), data.size())) {
        return false;
    }

    if (crypt == 1) {
        for (std::size_t i = 0; i < data.size() && i < 16; ++i) {
            data[i] ^= 0xcf;
        }
    } else if (crypt == 2) {
        if (compress == 0) {
            std::size_t produced = 0;
            if (!decryptCarrier(data, decoded, reinterpret_cast<unsigned char*>(result.get()), decoded, produced) ||
                produced != decoded) return false;
            size = decoded;
            out = std::move(result);
            return true;
        }
        // Compressed AES still needs a decrypted input buffer, as in 180089800.
        std::size_t plain_size = 0;
        if (!carrierSize(data, plain_size)) return false;
        std::vector<unsigned char> plain(plain_size);
        std::size_t produced = 0;
        if (!decryptCarrier(data, decoded, plain.data(), plain.size(), produced)) return false;
        data = std::move(plain);
    }

    if (compress == 1) {
        // Fixed output allocation, like native uncompress. The substitute stb
        // API still has signed-int capacities; larger compressed buffers remain
        // an explicit unsupported case, not silently narrowed to negative ints.
        if (decoded > static_cast<std::uint32_t>(INT_MAX) || data.size() > INT_MAX) return false;
        const int produced = stbi_zlib_decode_buffer(reinterpret_cast<char*>(result.get()),
            static_cast<int>(decoded), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
        if (produced < 0 || static_cast<std::uint32_t>(produced) != decoded) return false;
    } else if (data.size() < decoded) {
        return false;
    } else if (decoded != 0) {
        std::memcpy(result.get(), data.data(), decoded);
    }
    size = decoded;
    out = std::move(result);
    return true;
}

bool WarcArchive::read(std::uint64_t value, std::vector<unsigned char>& out) const {
    if (value > data_.size) {
        return false;
    }
    Section member;
    member.payload = data_.payload + value;
    member.size = data_.size - value;
    return readSection(member, out);
}

bool WarcArchive::readOwned(std::uint64_t value, std::unique_ptr<std::byte[]>& out, std::uint32_t& size) const {
    if (value > data_.size) return false;
    return readSectionOwned(Section{data_.payload + value, data_.size - value}, out, size);
}

bool WarcArchive::readHead(unsigned char* out, std::size_t size) const {
    if (!stream_) return false;
    std::lock_guard<std::mutex> lock(g_mounted_archive_reads);
    HANDLE file = stream_->handle;
    RestoreFilePosition restore(file);
    if (!restore.valid || size > MAXDWORD || !SetFilePointerEx(file, LARGE_INTEGER{}, nullptr, FILE_BEGIN)) return false;
    DWORD completed = 0;
    ReadFile(file, out, static_cast<DWORD>(size), &completed, nullptr); // short-read tail stays untouched
    return true;
}

const std::vector<WarcName>& WarcArchive::names() const {
    if (names_loaded_) {
        return names_;
    }
    names_loaded_ = true;
    std::vector<unsigned char> buffer;
    if (!has_names_ || !readSection(names_section_, buffer)) {
        return names_;
    }
    std::size_t at = 0;
    while (at + 12 <= buffer.size()) {
        WarcName entry;
        entry.key = readU64(buffer.data() + at);
        const std::size_t characters = readU32(buffer.data() + at + 8);
        at += 12;
        if (characters > (buffer.size() - at) / 2) {
            break;
        }
        entry.name.resize(characters);
        std::memcpy(entry.name.data(), buffer.data() + at, characters * 2);
        at += characters * 2;
        names_.push_back(std::move(entry));
    }
    return names_;
}

const MsvcOrderedNameMap& WarcArchive::nameMap() const {
    if (!name_map_built_) {
        name_map_built_ = true;
        for (const auto& entry : names()) {
            name_map_.tryEmplace(entry.key, entry.name);
        }
    }
    return name_map_;
}

bool WarcArchive::descendDirectories(
    int which, std::size_t table, const std::vector<std::uint64_t>& keys, std::size_t& out) const {
    if (which < 0 || which > 1 || !tableValid(which, table)) {
        return false;
    }
    for (const auto key : keys) {
        if (!lookupDirectory(which, table, key, table)) {
            return false;
        }
    }
    out = table;
    return true;
}

bool WarcArchive::lookupInTable(
    int which, std::size_t table, std::uint64_t key, bool recurse, std::uint64_t& value) const {
    if (which < 0 || which > 1 || !tableValid(which, table)) {
        return false;
    }
    return lookupFile(which, table, key, value, recurse);
}

bool WarcArchive::readTable(int which, std::size_t table, TableContents& out) const {
    if (which < 0 || which > 1 || !tableValid(which, table)) {
        return false;
    }
    const auto& buffer = tables_[which];
    const unsigned char* base = buffer.data() + table;
    const std::uint64_t header = readU32(base);
    const std::uint64_t dirs = readU32(base + 0x10);
    const std::uint64_t files = readU32(base + 0x14);
    out.files.clear();
    out.directories.clear();
    for (std::uint64_t i = 0; i < dirs; ++i) {
        const auto* record = base + header + i * kRecordSize;
        const std::uint64_t relative = readU64(record + 8);
        if (relative > buffer.size() - table) {
            continue;
        }
        out.directories.emplace_back(readU64(record), table + static_cast<std::size_t>(relative));
    }
    for (std::uint64_t i = 0; i < files; ++i) {
        const auto* record = base + header + (dirs + i) * kRecordSize;
        out.files.emplace_back(readU64(record), readU64(record + 8));
    }
    return true;
}

} // namespace cm3d2::internal
