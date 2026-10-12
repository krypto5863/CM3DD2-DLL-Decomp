#include "file_system_map_abi.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace cm3d2::internal::map_abi {
namespace {

struct RawString {            // MSVC std::wstring layout
    union {
        wchar_t buffer[8];
        wchar_t* pointer;
    };
    std::uint64_t size;
    std::uint64_t capacity;   // 7 when the text lives in the buffer
};
static_assert(sizeof(RawString) == 32);

struct Image {
    float max_load;
    std::uint32_t pad;
    char* head;
    std::uint64_t size;
    char** buckets_first;
    char** buckets_last;
    char** buckets_end;
    std::uint64_t mask;
    std::uint64_t maxidx;
};
static_assert(sizeof(Image) == kImageSize);

std::size_t nodeSize(Kind kind) { return kind == Kind::Set ? kSetNodeSize : kMapNodeSize; }
std::size_t stringOffset(Kind kind) { return kind == Kind::Set ? 0x10 : 0x18; }

char*& next(char* node) { return *reinterpret_cast<char**>(node); }
char*& prev(char* node) { return *reinterpret_cast<char**>(node + 8); }
char* const& nextOf(const char* node) { return *reinterpret_cast<char* const*>(node); }
RawString* stringOf(char* node, Kind kind) { return reinterpret_cast<RawString*>(node + stringOffset(kind)); }
const RawString* stringOf(const char* node, Kind kind) { return reinterpret_cast<const RawString*>(node + stringOffset(kind)); }

// May throw std::bad_alloc; on a throw `target` owns nothing.
void writeString(RawString* target, const std::wstring& value) {
    std::memset(target, 0, sizeof(RawString));
    target->size = value.size();
    if (value.size() <= 7) {
        target->capacity = 7;
        std::memcpy(target->buffer, value.data(), value.size() * sizeof(wchar_t));
        return;
    }
    const std::uint64_t capacity = static_cast<std::uint64_t>(value.size()) | 7U;
    auto* text = static_cast<wchar_t*>(::operator new((capacity + 1) * sizeof(wchar_t)));
    std::memcpy(text, value.data(), value.size() * sizeof(wchar_t));
    text[value.size()] = L'\0';
    target->pointer = text;
    target->capacity = capacity;
}

std::wstring readString(const RawString* source) {
    const wchar_t* text = source->capacity > 7 ? source->pointer : source->buffer;
    return std::wstring(text, static_cast<std::size_t>(source->size));
}

void freeString(RawString* value) noexcept {
    if (value->capacity > 7) {
        ::operator delete(value->pointer);
    }
}

void freeNodes(Image* image, Kind kind) noexcept {
    char* head = image->head;
    char* node = next(head);
    while (node != head) {
        char* following = next(node);
        freeString(stringOf(node, kind));
        ::operator delete(node);
        node = following;
    }
    next(head) = head;
    prev(head) = head;
    image->size = 0;
}

void releaseImage(Image* image, Kind kind) noexcept {
    if (image->head != nullptr) {
        freeNodes(image, kind);
        ::operator delete(image->head);
        ::operator delete(image->buckets_first);
    }
}

char* newNode(Kind kind) {
    char* node = static_cast<char*>(::operator new(nodeSize(kind)));
    std::memset(node, 0, nodeSize(kind));
    return node;
}

// A complete replacement state built off to the side. Everything it owns is released on destruction unless publish()
// moved it into an image. Construction steps may throw std::bad_alloc; nothing visible has changed until publish().
class Pending {
public:
    explicit Pending(Kind kind) : kind_(kind) {
        image_.max_load = 1.0F;
        image_.head = newNode(kind);
        next(image_.head) = image_.head;
        prev(image_.head) = image_.head;
    }
    Pending(const Pending&) = delete;
    Pending& operator=(const Pending&) = delete;
    ~Pending() { releaseImage(&image_, kind_); }

    void buckets(std::size_t bucket_count) {
        auto** pairs = static_cast<char**>(::operator new(bucket_count * 2 * sizeof(char*)));
        for (std::size_t i = 0; i < bucket_count * 2; ++i) {
            pairs[i] = image_.head;
        }
        image_.buckets_first = pairs;
        image_.buckets_last = pairs + bucket_count * 2;
        image_.buckets_end = image_.buckets_last;
        image_.mask = bucket_count - 1;
        image_.maxidx = bucket_count;
    }

    // Links a fully built node at the tail and records it in its bucket pair (lo = first seen, hi = last seen). Never throws.
    void append(char* node, std::uint64_t hash) noexcept {
        char* head = image_.head;
        next(node) = head;
        prev(node) = prev(head);
        next(prev(head)) = node;
        prev(head) = node;
        ++image_.size;
        char** pair = image_.buckets_first + static_cast<std::size_t>(hash & image_.mask) * 2;
        if (pair[0] == head) {
            pair[0] = node;
        }
        pair[1] = node;
    }

    // Moves the state into `target`, releasing what `target` held before. Never throws.
    void publish(Image* target) noexcept {
        Image old = *target;
        *target = image_;
        std::memset(&image_, 0, sizeof image_);
        releaseImage(&old, kind_);
    }

private:
    Kind kind_;
    Image image_{};
};

bool validBucketCount(std::size_t bucket_count) {
    return bucket_count >= 8 && bucket_count <= (static_cast<std::size_t>(1) << 26) && (bucket_count & (bucket_count - 1)) == 0;
}

// Every bucket's nodes must form a single run in the given order.
bool contiguous(const std::vector<std::size_t>& bucket_of_node, std::size_t bucket_count) {
    std::vector<unsigned char> finished(bucket_count, 0);
    std::size_t current = bucket_count;   // none yet
    for (const std::size_t bucket : bucket_of_node) {
        if (bucket == current) {
            continue;
        }
        if (finished[bucket] != 0) {
            return false;
        }
        if (current != bucket_count) {
            finished[current] = 1;
        }
        current = bucket;
    }
    return true;
}

} // namespace

std::uint64_t hashSetKey(const std::wstring& value) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    for (std::size_t i = 0; i < value.size() * 2; ++i) {
        h = (h ^ bytes[i]) * 0x100000001b3ULL;
    }
    return h;
}

std::uint64_t hashMapKey(std::uint64_t key) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 8; ++i) {
        h = (h ^ ((key >> (8 * i)) & 0xff)) * 0x100000001b3ULL;
    }
    return h;
}

std::size_t geometryFor(std::size_t count) {
    // Replays the growth rule of FUN_1800843e0 / FUN_1800862f0: rehash when (size+1)/buckets > 1.
    std::size_t buckets = 8;
    for (std::size_t size = 0; size < count; ++size) {
        if (static_cast<double>(size + 1) / static_cast<double>(buckets) > 1.0) {
            const std::size_t desired = std::max<std::size_t>(8, size + 1);
            std::size_t target = buckets;
            if (buckets < desired) {
                target = buckets > 0x1ff ? desired : std::max(buckets * 8, desired);
            }
            const std::size_t base = (target - 1) | 1;
            std::size_t log = 0;
            while ((base >> (log + 1)) != 0) {
                ++log;
            }
            buckets = static_cast<std::size_t>(1) << (log + 1);
        }
    }
    return buckets;
}

Status construct(void* storage, Kind kind) noexcept {
    auto* image = static_cast<Image*>(storage);
    std::memset(image, 0, sizeof(Image));
    try {
        Pending pending(kind);
        pending.buckets(8);
        pending.publish(image);
        return Status::Ok;
    } catch (...) {
        return Status::AllocationFailed;
    }
}

void destroy(void* storage, Kind kind) noexcept {
    auto* image = static_cast<Image*>(storage);
    releaseImage(image, kind);
    std::memset(image, 0, sizeof(Image));
}

Status assignSet(void* storage, const std::vector<std::wstring>& ordered, std::size_t bucket_count) noexcept {
    auto* image = static_cast<Image*>(storage);
    try {
        if (bucket_count == 0) {
            bucket_count = geometryFor(ordered.size());
        }
        if (!validBucketCount(bucket_count)) {
            return Status::BadBucketCount;
        }
        std::vector<std::size_t> buckets;
        buckets.reserve(ordered.size());
        for (const auto& value : ordered) {
            buckets.push_back(static_cast<std::size_t>(hashSetKey(value) & (bucket_count - 1)));
        }
        if (!contiguous(buckets, bucket_count)) {
            return Status::NotContiguous;
        }
        std::vector<const std::wstring*> sorted;
        sorted.reserve(ordered.size());
        for (const auto& value : ordered) {
            sorted.push_back(&value);
        }
        std::sort(sorted.begin(), sorted.end(), [](const std::wstring* l, const std::wstring* r) { return *l < *r; });
        if (std::adjacent_find(sorted.begin(), sorted.end(), [](const std::wstring* l, const std::wstring* r) { return *l == *r; }) != sorted.end()) {
            return Status::DuplicateKey;
        }
        Pending pending(Kind::Set);
        pending.buckets(bucket_count);
        for (const auto& value : ordered) {
            char* node = newNode(Kind::Set);
            try {
                writeString(stringOf(node, Kind::Set), value);
            } catch (...) {
                ::operator delete(node);
                throw;
            }
            pending.append(node, hashSetKey(value));
        }
        pending.publish(image);
        return Status::Ok;
    } catch (...) {
        return Status::AllocationFailed;
    }
}

Status assignMap(void* storage, const std::vector<std::pair<std::uint64_t, std::wstring>>& ordered, std::size_t bucket_count) noexcept {
    auto* image = static_cast<Image*>(storage);
    try {
        if (bucket_count == 0) {
            bucket_count = geometryFor(ordered.size());
        }
        if (!validBucketCount(bucket_count)) {
            return Status::BadBucketCount;
        }
        std::vector<std::size_t> buckets;
        std::vector<std::uint64_t> keys;
        buckets.reserve(ordered.size());
        keys.reserve(ordered.size());
        for (const auto& entry : ordered) {
            buckets.push_back(static_cast<std::size_t>(hashMapKey(entry.first) & (bucket_count - 1)));
            keys.push_back(entry.first);
        }
        if (!contiguous(buckets, bucket_count)) {
            return Status::NotContiguous;
        }
        std::sort(keys.begin(), keys.end());
        if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
            return Status::DuplicateKey;
        }
        Pending pending(Kind::Map);
        pending.buckets(bucket_count);
        for (const auto& entry : ordered) {
            char* node = newNode(Kind::Map);
            try {
                *reinterpret_cast<std::uint64_t*>(node + 0x10) = entry.first;
                writeString(stringOf(node, Kind::Map), entry.second);
            } catch (...) {
                ::operator delete(node);
                throw;
            }
            pending.append(node, hashMapKey(entry.first));
        }
        pending.publish(image);
        return Status::Ok;
    } catch (...) {
        return Status::AllocationFailed;
    }
}

std::size_t sizeOf(const void* storage) {
    return static_cast<std::size_t>(static_cast<const Image*>(storage)->size);
}

std::vector<std::wstring> readSet(const void* storage) {
    const auto* image = static_cast<const Image*>(storage);
    std::vector<std::wstring> out;
    if (image->head == nullptr) {
        return out;
    }
    for (const char* node = nextOf(image->head); node != image->head; node = nextOf(node)) {
        out.push_back(readString(stringOf(node, Kind::Set)));
    }
    return out;
}

std::vector<std::pair<std::uint64_t, std::wstring>> readMap(const void* storage) {
    const auto* image = static_cast<const Image*>(storage);
    std::vector<std::pair<std::uint64_t, std::wstring>> out;
    if (image->head == nullptr) {
        return out;
    }
    for (const char* node = nextOf(image->head); node != image->head; node = nextOf(node)) {
        out.emplace_back(*reinterpret_cast<const std::uint64_t*>(node + 0x10), readString(stringOf(node, Kind::Map)));
    }
    return out;
}

} // namespace cm3d2::internal::map_abi
