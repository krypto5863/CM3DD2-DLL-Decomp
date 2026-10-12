// Valid snapshots only: builds raw MSVC-layout images from ordered content, reads them back the way the native consumers do
// (head->next walk, size at +0x10, wstring SSO rules), checks prev links / string terminators / bucket pairs on the raw
// bytes, rejects unusable input, and injects allocation failures to prove cleanup and "unchanged on failure".
#include "file_system_map_abi.h"
#include "msvc_ordered_map.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>

// ---- allocation accounting / failure injection (whole test program) -------------------------------------------
namespace {
std::atomic<long long> g_live{0};
std::atomic<long long> g_countdown{-1};   // >= 0: the allocation that finds 0 throws; -1 = off
}

void* operator new(std::size_t size) {
    const long long left = g_countdown.load();
    if (left == 0) {
        g_countdown.store(-1);
        throw std::bad_alloc();
    }
    if (left > 0) {
        g_countdown.store(left - 1);
    }
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc();
    }
    ++g_live;
    return pointer;
}
void operator delete(void* pointer) noexcept {
    if (pointer != nullptr) {
        --g_live;
        std::free(pointer);
    }
}
void operator delete(void* pointer, std::size_t) noexcept { operator delete(pointer); }

namespace {

using namespace cm3d2::internal;
using namespace cm3d2::internal::map_abi;

void check(bool condition, const char* message) {
    if (!condition) {
        g_countdown.store(-1);
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::uint64_t u64At(const void* base, std::size_t offset) {
    std::uint64_t value;
    std::memcpy(&value, static_cast<const char*>(base) + offset, sizeof value);
    return value;
}

const char* at(const void* pointer) { return static_cast<const char*>(pointer); }
const char* linkOf(const char* node, std::size_t offset) {
    const char* value;
    std::memcpy(&value, node + offset, sizeof value);
    return value;
}

// Raw string view over a node's wstring field.
struct RawView {
    const wchar_t* text;
    std::uint64_t size;
    std::uint64_t capacity;
};
RawView rawString(const char* field) {
    RawView view{};
    view.size = u64At(field, 0x10);
    view.capacity = u64At(field, 0x18);
    if (view.capacity > 7) {
        const wchar_t* pointer;
        std::memcpy(&pointer, field, sizeof pointer);
        view.text = pointer;
    } else {
        view.text = reinterpret_cast<const wchar_t*>(field);
    }
    return view;
}

// Independent walk of the raw bytes: next links forward, prev links backward, terminators, bucket runs.
template <class HashOfNode>
void checkRaw(const void* image, Kind kind, std::size_t node_count, HashOfNode hash_of_node) {
    const char* head = linkOf(at(image), 0x08);
    check(u64At(image, 0x10) == node_count, "size field");
    const std::size_t string_offset = kind == Kind::Set ? 0x10 : 0x18;

    std::vector<const char*> forward;
    for (const char* node = linkOf(head, 0); node != head; node = linkOf(node, 0)) {
        check(forward.size() <= node_count, "forward walk longer than size (cycle?)");
        forward.push_back(node);
    }
    check(forward.size() == node_count, "forward walk length equals size");
    std::vector<const char*> backward;
    for (const char* node = linkOf(head, 8); node != head; node = linkOf(node, 8)) {
        check(backward.size() <= node_count, "backward walk longer than size");
        backward.push_back(node);
    }
    std::reverse(backward.begin(), backward.end());
    check(backward == forward, "prev links mirror next links");
    if (!forward.empty()) {
        check(linkOf(forward.front(), 8) == head && linkOf(forward.back(), 0) == head, "ends link to the sentinel");
    } else {
        check(linkOf(head, 0) == head && linkOf(head, 8) == head, "empty sentinel links to itself");
    }

    for (const char* node : forward) {
        const RawView view = rawString(node + string_offset);
        check(view.capacity >= 7 && view.capacity >= view.size, "capacity covers size");
        check((view.capacity > 7) == (view.size > 7), "heap iff size > 7");
        check(view.text[view.size] == L'\0', "string terminator");
    }

    const auto* pairs = reinterpret_cast<const char* const*>(u64At(image, 0x18));
    const auto* pairs_end = reinterpret_cast<const char* const*>(u64At(image, 0x28));
    const std::uint64_t mask = u64At(image, 0x30);
    check(u64At(image, 0x38) == mask + 1, "maxidx is mask+1");
    check(static_cast<std::size_t>(pairs_end - pairs) == (mask + 1) * 2, "two pointers per bucket");
    check(u64At(image, 0x20) == u64At(image, 0x28), "vector last == end");
    std::size_t covered = 0;
    for (std::uint64_t bucket = 0; bucket <= mask; ++bucket) {
        const char* lo = pairs[bucket * 2];
        const char* hi = pairs[bucket * 2 + 1];
        if (lo == head) {
            check(hi == head, "empty bucket has both ends at the sentinel");
            continue;
        }
        for (const char* node = lo;; node = linkOf(node, 0)) {
            check(node != head, "bucket range ran into the sentinel");
            check((hash_of_node(node) & mask) == bucket, "node sits in its own bucket range");
            ++covered;
            if (node == hi) break;
        }
    }
    check(covered == node_count, "bucket ranges cover every node exactly once");
}

std::uint64_t setHash(const char* node) {
    const RawView view = rawString(node + 0x10);
    return hashSetKey(std::wstring(view.text, static_cast<std::size_t>(view.size)));
}
std::uint64_t mapHash(const char* node) { return hashMapKey(u64At(node, 0x10)); }

std::wstring sampleName(int index) {
    static const wchar_t* shorts[] = {L"a.menu", L"b", L"", L"abc.mnu", L"1234567"};
    if (index % 7 == 0) return shorts[(index / 7) % 5];
    return L"menu\\body\\item_" + std::to_wstring(index) + L"_i_.menu";
}

void content(int count, std::vector<std::wstring>& set, std::vector<std::pair<std::uint64_t, std::wstring>>& map) {
    MsvcOrderedStringSet ordered_set;
    MsvcOrderedNameMap ordered_map;
    for (int i = 0; i < count; ++i) {
        ordered_set.insert(sampleName(i) + L"#" + std::to_wstring(i));
        ordered_map.tryEmplace(0x9E3779B97F4A7C15ULL * static_cast<std::uint64_t>(i + 1), sampleName(i));
    }
    set = ordered_set.values();
    map.clear();
    for (const auto& entry : ordered_map.entries()) map.emplace_back(entry.first, *entry.second);
}

void roundTrips() {
    alignas(16) unsigned char set_image[kImageSize];
    alignas(16) unsigned char map_image[kImageSize];
    check(construct(set_image, Kind::Set) == Status::Ok && construct(map_image, Kind::Map) == Status::Ok, "construct");
    check(sizeOf(set_image) == 0 && readSet(set_image).empty() && sizeOf(map_image) == 0 && readMap(map_image).empty(), "empty images");
    float load;
    std::memcpy(&load, set_image, sizeof load);
    check(load == 1.0F && u64At(set_image, 0x30) == 7 && u64At(set_image, 0x38) == 8, "initial geometry 8 buckets, load 1.0");
    checkRaw(set_image, Kind::Set, 0, setHash);
    checkRaw(map_image, Kind::Map, 0, mapHash);

    for (int count : {1, 2, 7, 8, 9, 40, 64, 65, 130, 600}) {
        std::vector<std::wstring> names;
        std::vector<std::pair<std::uint64_t, std::wstring>> entries;
        content(count, names, entries);
        check(assignSet(set_image, names) == Status::Ok, "assignSet");
        check(readSet(set_image) == names, "set content and order");
        checkRaw(set_image, Kind::Set, names.size(), setHash);
        check(assignMap(map_image, entries) == Status::Ok, "assignMap");
        check(readMap(map_image) == entries, "map content and order");
        checkRaw(map_image, Kind::Map, entries.size(), mapHash);
    }

    check(assignSet(set_image, {L"x", L"y"}) == Status::Ok && readSet(set_image) == std::vector<std::wstring>({L"x", L"y"}), "shrunk set");
    check(assignSet(set_image, {}) == Status::Ok && sizeOf(set_image) == 0, "emptied set");
    destroy(set_image, Kind::Set);
    destroy(map_image, Kind::Map);
    destroy(set_image, Kind::Set);   // idempotent on a zeroed image
}

void rejections() {
    alignas(16) unsigned char image[kImageSize];
    check(construct(image, Kind::Set) == Status::Ok, "construct");
    std::vector<std::wstring> names;
    std::vector<std::pair<std::uint64_t, std::wstring>> entries;
    content(40, names, entries);
    check(assignSet(image, names) == Status::Ok, "baseline");

    for (std::size_t bad : {std::size_t{1}, std::size_t{3}, std::size_t{6}, std::size_t{12}, std::size_t{100}, std::size_t{1} << 27}) {
        check(assignSet(image, {L"q"}, bad) == Status::BadBucketCount, "non power-of-two / out-of-range bucket count rejected");
    }
    check(readSet(image) == names, "rejection leaves the image unchanged");

    // A geometry that differs from the real one scatters buckets: some order is not a single run per bucket.
    bool saw_not_contiguous = false;
    for (std::size_t buckets : {std::size_t{8}, std::size_t{16}, std::size_t{128}, std::size_t{1024}}) {
        if (assignSet(image, names, buckets) == Status::NotContiguous) saw_not_contiguous = true;
    }
    check(saw_not_contiguous, "a wrong geometry is detected as non-contiguous");
    check(readSet(image) == names, "non-contiguous rejection leaves the image unchanged");

    std::vector<std::wstring> shuffled = names;
    std::reverse(shuffled.begin(), shuffled.end());
    std::rotate(shuffled.begin(), shuffled.begin() + 1, shuffled.end());
    // Exact order check: reversing a multi-bucket order keeps runs contiguous, but an interleaving breaks them.
    std::vector<std::wstring> interleaved;
    for (std::size_t i = 0; i < names.size() / 2; ++i) {
        interleaved.push_back(names[i]);
        interleaved.push_back(names[names.size() - 1 - i]);
    }
    check(assignSet(image, interleaved, geometryFor(names.size())) == Status::NotContiguous || interleaved.size() < 3, "interleaved order rejected");

    check(assignSet(image, {L"dup", L"dup"}) == Status::DuplicateKey, "duplicate set values rejected");
    destroy(image, Kind::Set);

    alignas(16) unsigned char map_image[kImageSize];
    check(construct(map_image, Kind::Map) == Status::Ok, "construct map");
    check(assignMap(map_image, {{7, L"a"}, {7, L"b"}}) == Status::DuplicateKey, "duplicate map keys rejected");
    check(sizeOf(map_image) == 0, "duplicate rejection publishes nothing");
    destroy(map_image, Kind::Map);
}

// Fails the n-th allocation inside the call and checks: status, unchanged image, no leaks, retry success.
void allocationFailures() {
    std::vector<std::wstring> before_names, after_names;
    std::vector<std::pair<std::uint64_t, std::wstring>> before_entries, after_entries;
    content(12, before_names, before_entries);
    content(21, after_names, after_entries);

    int set_failures = 0, map_failures = 0;
    const long long baseline = g_live.load();
    {
        alignas(16) unsigned char image[kImageSize];
        for (int fail_at = 0; fail_at < 80; ++fail_at) {
            // construct under failure: zeroed image, no leak
            g_countdown.store(fail_at);
            const Status built = construct(image, Kind::Set);
            g_countdown.store(-1);
            if (built != Status::Ok) {
                check(built == Status::AllocationFailed, "construct failure status");
                check(u64At(image, 0x08) == 0, "failed construct leaves a zeroed image");
                destroy(image, Kind::Set);
                continue;
            }
            check(assignSet(image, before_names) == Status::Ok, "seed set");

            g_countdown.store(fail_at);
            const Status status = assignSet(image, after_names);
            g_countdown.store(-1);
            if (status == Status::Ok) {
                check(readSet(image) == after_names, "success publishes the new content");
                checkRaw(image, Kind::Set, after_names.size(), setHash);
            } else {
                check(status == Status::AllocationFailed, "failure status");
                ++set_failures;
                check(readSet(image) == before_names, "failed assign leaves the previous content");
                checkRaw(image, Kind::Set, before_names.size(), setHash);
                check(assignSet(image, after_names) == Status::Ok && readSet(image) == after_names, "retry after failure");
            }
            destroy(image, Kind::Set);
        }

        for (int fail_at = 0; fail_at < 80; ++fail_at) {
            check(construct(image, Kind::Map) == Status::Ok, "construct map");
            check(assignMap(image, before_entries) == Status::Ok, "seed map");
            g_countdown.store(fail_at);
            const Status status = assignMap(image, after_entries);
            g_countdown.store(-1);
            if (status == Status::Ok) {
                check(readMap(image) == after_entries, "map success");
            } else {
                check(status == Status::AllocationFailed, "map failure status");
                ++map_failures;
                check(readMap(image) == before_entries, "failed map assign leaves the previous content");
                check(assignMap(image, after_entries) == Status::Ok && readMap(image) == after_entries, "map retry");
            }
            destroy(image, Kind::Map);
        }
    }
    check(set_failures > 10 && map_failures > 10, "the injection really hit allocations in both assign paths");
    check(g_live.load() == baseline, "no allocation leaked across all injected failures");
}

} // namespace

int main() {
    roundTrips();
    rejections();
    allocationFailures();
    std::cout << "file_system_map_abi_contract: ok\n";
    return 0;
}
