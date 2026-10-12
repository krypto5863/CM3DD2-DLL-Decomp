#include "kag_serialize.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace cm3d2::kag {

namespace {

constexpr std::uint32_t kMagic = 0x61646c72;  // "rlda"

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void putWide(std::vector<std::uint8_t>& out, std::wstring_view text) {
    put32(out, static_cast<std::uint32_t>(text.size() * 2 + 2));  // bytes including the terminator
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    out.insert(out.end(), bytes, bytes + text.size() * 2);
    out.push_back(0);
    out.push_back(0);
}

// Bounded little-endian reader. The original reads a bare pointer; `limit` hardens it.
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t limit) : data_(data), limit_(limit) {}
    std::size_t position() const { return position_; }
    bool good() const { return good_; }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        if (!take(4)) return 0;
        std::memcpy(&value, data_ + position_ - 4, 4);
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        if (!take(8)) return 0;
        std::memcpy(&value, data_ + position_ - 8, 8);
        return value;
    }
    std::uint8_t u8() {
        if (!take(1)) return 0;
        return data_[position_ - 1];
    }
    // A NUL terminated wide string occupying `bytes` bytes (the original uses wcslen on the buffer and skips `bytes`).
    std::wstring wide(std::uint32_t bytes) {
        const std::size_t start = position_;
        std::wstring text;
        if (!take(bytes)) return text;
        const std::size_t max_chars = std::min<std::size_t>(bytes / 2, (limit_ - start) / 2);
        for (std::size_t i = 0; i < max_chars; ++i) {
            wchar_t c = 0;
            std::memcpy(&c, data_ + start + i * 2, 2);
            if (c == 0) break;
            text.push_back(c);
        }
        return text;
    }
    bool take(std::size_t count) {
        if (!good_ || count > limit_ - position_) {
            good_ = false;
            return false;
        }
        position_ += count;
        return true;
    }

private:
    const std::uint8_t* data_;
    std::size_t limit_;
    std::size_t position_ = 0;
    bool good_ = true;
};

} // namespace

std::uint64_t hashUtf16(std::wstring_view text) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    for (std::size_t i = 0; i < text.size() * 2; ++i) {
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    }
    return hash;
}

bool RecordDb::add(std::uint64_t storage, std::uint64_t label) {
    return entries_[storage].insert(label).second;
}

bool RecordDb::contains(std::uint64_t storage, std::uint64_t label) const {
    const auto found = entries_.find(storage);
    return found != entries_.end() && found->second.count(label) != 0;
}

std::vector<std::uint8_t> RecordDb::serialize() const {
    std::size_t labels = 0;
    for (const auto& entry : entries_) labels += entry.second.size();
    std::vector<std::uint8_t> out;
    put32(out, static_cast<std::uint32_t>(entries_.size() * 12 + 8 + labels * 8));
    put32(out, kMagic);
    put32(out, static_cast<std::uint32_t>(entries_.size()));
    for (const auto& entry : entries_) {
        put64(out, entry.first);
        put32(out, static_cast<std::uint32_t>(entry.second.size()));
        for (const auto label : entry.second) put64(out, label);
    }
    return out;
}

bool RecordDb::deserialize(const std::uint8_t* block, std::size_t limit) {
    entries_.clear();
    Reader reader(block, limit);
    if (reader.u32() != kMagic || !reader.good()) return false;
    const std::uint32_t count = reader.u32();
    for (std::uint32_t i = 0; i < count && reader.good(); ++i) {
        const std::uint64_t storage = reader.u64();
        const std::uint32_t labels = reader.u32();
        auto& set = entries_[storage];
        for (std::uint32_t j = 0; j < labels && reader.good(); ++j) set.insert(reader.u64());
    }
    return reader.good();
}

void CallRecords::push(std::wstring storage, std::wstring label, std::int32_t id) {
    entries_.push_front(CallRecord{std::move(storage), std::move(label), id});
}

bool CallRecords::pop(std::wstring& storage, std::wstring& label) {
    if (entries_.empty()) return false;
    const CallRecord& front = entries_.front();
    storage = front.storage;
    label = L"*RET_" + front.label;
    if (front.id >= 0) label += L"_" + std::to_wstring(front.id);
    entries_.pop_front();
    return true;
}

std::vector<std::uint8_t> serializeSaveData(const RecordDb& records, const std::optional<std::wstring>& state_text,
    bool flag, const CallRecords& calls) {
    std::vector<std::uint8_t> out = records.serialize();
    if (state_text.has_value()) {
        putWide(out, *state_text);
    } else {
        put32(out, 0);
    }
    out.push_back(flag ? 1 : 0);
    put32(out, static_cast<std::uint32_t>(calls.size()));
    for (const auto& call : calls.entries()) {
        putWide(out, call.storage);
        putWide(out, call.label);
        put32(out, static_cast<std::uint32_t>(call.id));
    }
    return out;
}

ParsedSaveData parseSaveData(const std::uint8_t* data, std::size_t limit, RecordDb& records, CallRecords& calls) {
    ParsedSaveData parsed;
    Reader header(data, limit);
    const std::uint32_t block_bytes = header.u32();
    if (!header.good() || block_bytes > limit - 4) return parsed;
    records.deserialize(data + 4, limit - 4);
    Reader reader(data + 4 + block_bytes, limit - 4 - block_bytes);
    const std::uint32_t text_bytes = reader.u32();
    if (text_bytes != 0) {
        parsed.state_text = reader.wide(text_bytes);
    }
    parsed.flag = reader.u8() != 0;
    calls.clear();
    const std::int32_t count = static_cast<std::int32_t>(reader.u32());
    for (std::int32_t i = 0; i < count && reader.good(); ++i) {
        const std::uint32_t storage_bytes = reader.u32();
        std::wstring storage = reader.wide(storage_bytes);
        const std::uint32_t label_bytes = reader.u32();
        std::wstring label = reader.wide(label_bytes);
        const std::int32_t id = static_cast<std::int32_t>(reader.u32());
        calls.entries().push_back(CallRecord{std::move(storage), std::move(label), id});
    }
    parsed.consumed = 4 + block_bytes + reader.position();
    return parsed;
}

} // namespace cm3d2::kag
