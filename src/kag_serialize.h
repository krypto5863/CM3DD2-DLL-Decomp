#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// KAG record database and save-data layout, reconstructed from BasicKagScript/KagScript
// (FUN_180102ce0 core serializer, FUN_180103010 core reader, FUN_180105e50 / FUN_180106010 wrappers,
// FUN_180107f80 record reader, FUN_180104540 / FUN_180104370 / FUN_180104780 record-call list,
// FUN_180108280 / FUN_1801083e0 add and lookup, FUN_180107e20 / FUN_180107ed0 hashing wrappers).
// Behaviour was also confirmed against the original DLL (tools/differential/kag_serialize_probe.cpp).
namespace cm3d2::kag {

// FNV-1a 64 over the UTF-16LE bytes of the text (the key used by DLL_KAG_GetKey and the record database).
std::uint64_t hashUtf16(std::wstring_view text);

// DLL_KAG_AddFinishFile stores the second key 0x3675e1bef4f25d36 under the file name hash.
constexpr std::uint64_t kFinishKey = 0x3675e1bef4f25d36ull;

// map<storage key, set<label key>> (the object at script +0xc0).
class RecordDb {
public:
    bool add(std::uint64_t storage, std::uint64_t label);  // true when the label was new
    bool contains(std::uint64_t storage, std::uint64_t label) const;
    bool addLabel(std::wstring_view storage, std::wstring_view label) { return add(hashUtf16(storage), hashUtf16(label)); }
    bool hasLabel(std::wstring_view storage, std::wstring_view label) const { return contains(hashUtf16(storage), hashUtf16(label)); }
    bool addFinish(std::wstring_view storage) { return add(hashUtf16(storage), kFinishKey); }
    bool hasFinish(std::wstring_view storage) const { return contains(hashUtf16(storage), kFinishKey); }
    void clear() { entries_.clear(); }
    std::size_t storageCount() const { return entries_.size(); }

    // [u32 size][u32 'rlda'][u32 count] then per storage {u64 key, u32 n, n * u64} in ascending order;
    // size counts everything after the size field (count*12 + 8 + labels*8).
    std::vector<std::uint8_t> serialize() const;
    // Clears the database first; false when the magic is wrong (the database stays empty).
    // `block` points just after the size field. `limit` bounds the reads.
    bool deserialize(const std::uint8_t* block, std::size_t limit);

private:
    std::map<std::uint64_t, std::set<std::uint64_t>> entries_;
};

// One entry of the record-call list (script +0x2a0, node +0x10 storage, +0x30 label, +0x50 id).
struct CallRecord {
    std::wstring storage;
    std::wstring label;
    std::int32_t id = 0;
};

// The list behaves as a stack at its front: r_call pushes, r_return pops.
class CallRecords {
public:
    void push(std::wstring storage, std::wstring label, std::int32_t id);
    // FUN_180104780: gives the stored storage and the return label "*RET_" + label (+ "_<id>" when id >= 0).
    bool pop(std::wstring& storage, std::wstring& label);
    void clear() { entries_.clear(); }
    std::size_t size() const { return entries_.size(); }
    const std::deque<CallRecord>& entries() const { return entries_; }
    std::deque<CallRecord>& entries() { return entries_; }

private:
    std::deque<CallRecord> entries_;
};

// Whole save-data blob (BasicKagScript::CreateSerializeData):
//   core = [record block][u32 T][T bytes of UTF-16 state text with terminator, T = 0 without text]
//   followed by the flag byte (+0x2b0) and [u32 count] { [u32 bytes][storage+NUL][u32 bytes][label+NUL][u32 id] }...
std::vector<std::uint8_t> serializeSaveData(const RecordDb& records, const std::optional<std::wstring>& state_text,
    bool flag, const CallRecords& calls);

struct ParsedSaveData {
    std::size_t consumed = 0;               // the wrapper returns this many bytes
    std::optional<std::wstring> state_text; // present when T != 0 (the caller restores the parser from it)
    bool flag = false;
};
// Replaces the records and the call list. `limit` bounds the reads (exports pass a very large limit because the
// original takes a bare pointer).
ParsedSaveData parseSaveData(const std::uint8_t* data, std::size_t limit, RecordDb& records, CallRecords& calls);

} // namespace cm3d2::kag
