#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace cm3d2::internal {

// The name maps are MSVC std::unordered_map<uint64_t, std::wstring> objects keyed by an FNV-1a
// hash of the key bytes (FUN_1800862f0, FUN_180087880, FUN_180075c70). Their iteration order is
// the list order those routines produce; this reproduces it.
class MsvcOrderedNameMap {
public:
    MsvcOrderedNameMap();
    // FUN_1800824f0 clears nodes without shrinking the existing bucket geometry.
    void clear();
    // Inserts when the key is absent (FUN_1800862f0).
    void tryEmplace(std::uint64_t key, const std::wstring& value);
    const std::wstring* find(std::uint64_t key) const;
    // Values in iteration order.
    std::vector<const std::wstring*> values() const;
    // Key/value pairs in iteration order (the order FUN_18007f450 writes the maps to a cache file).
    std::vector<std::pair<std::uint64_t, const std::wstring*>> entries() const;
    std::size_t size() const { return size_; }
    std::size_t bucketCount() const { return bucket_count_; }

private:
    struct Node {
        std::uint64_t key;
        std::wstring value;
    };
    using List = std::list<Node>;
    void rehash(std::size_t requested);
    std::size_t bucketOf(std::uint64_t key) const;

    List list_;
    std::vector<List::iterator> buckets_;  // lo/hi pair per bucket, list_.end() when empty
    std::size_t mask_ = 0;
    std::size_t bucket_count_ = 0;
    std::size_t size_ = 0;
};

} // namespace cm3d2::internal

namespace cm3d2::internal {

// std::unordered_set<std::wstring> as the menu-name sets use it (FUN_1800843e0, FUN_180085780):
// FNV-1a over the UTF-16 bytes without the fold, 8 initial buckets, the same insert-in-front
// and rehash rules as MsvcOrderedNameMap.
class MsvcOrderedStringSet {
public:
    MsvcOrderedStringSet();
    bool insert(const std::wstring& value);
    std::vector<std::wstring> values() const { return {list_.begin(), list_.end()}; }
    std::size_t size() const { return list_.size(); }
    std::size_t bucketCount() const { return bucket_count_; }

private:
    using List = std::list<std::wstring>;
    static std::uint64_t hashOf(const std::wstring& value);
    void rehash(std::size_t requested);

    List list_;
    std::vector<List::iterator> buckets_;
    std::size_t mask_ = 0;
    std::size_t bucket_count_ = 0;
};

} // namespace cm3d2::internal

namespace cm3d2::internal {

// std::unordered_map<uint64_t, V> as the file system tree nodes use it (FUN_1800763f0 for the file maps, FUN_180075f00 for the
// child maps): FNV-1a over the 8 key bytes, 8 initial buckets, insert-in-front-of-bucket and the same rehash rule as
// MsvcOrderedNameMap. Iteration order is the list order, which is the order the cache writer emits.
template <class V>
class MsvcOrderedU64Map {
public:
    MsvcOrderedU64Map() { reset(); }
    void clear() {  // FUN_180075470: nodes go, the bucket geometry stays
        list_.clear();
        std::fill(buckets_.begin(), buckets_.end(), list_.end());
    }
    std::size_t size() const { return list_.size(); }
    bool empty() const { return list_.empty(); }
    V* find(std::uint64_t key) {
        auto it = findIt(key);
        return it == list_.end() ? nullptr : &it->value;
    }
    const V* find(std::uint64_t key) const {
        auto it = const_cast<MsvcOrderedU64Map*>(this)->findIt(key);
        return it == list_.end() ? nullptr : &it->value;
    }
    // Inserts when the key is absent and returns the stored value either way.
    template <class... Args>
    std::pair<V*, bool> tryEmplace(std::uint64_t key, Args&&... args) {
        if (auto it = findIt(key); it != list_.end()) return {&it->value, false};
        if (static_cast<double>(list_.size() + 1) / static_cast<double>(bucket_count_) > 1.0) {
            const std::size_t desired = std::max<std::size_t>(8, list_.size() + 1);
            std::size_t target = bucket_count_;
            if (bucket_count_ < desired) target = bucket_count_ > 0x1ff ? desired : std::max(bucket_count_ * 8, desired);
            rehash(target);
        }
        const auto bucket = bucketOf(key);
        const bool was_empty = buckets_[bucket * 2] == list_.end();
        const auto where = was_empty ? list_.end() : buckets_[bucket * 2];
        const auto inserted = list_.emplace(where, key, std::forward<Args>(args)...);
        buckets_[bucket * 2] = inserted;
        if (was_empty) buckets_[bucket * 2 + 1] = inserted;
        return {&inserted->value, true};
    }
    // Pairs in iteration order.
    template <class F>
    void forEach(F&& visit) const {
        for (const auto& node : list_) visit(node.key, node.value);
    }
    template <class F>
    void forEachMutable(F&& visit) {
        for (auto& node : list_) visit(node.key, node.value);
    }

private:
    struct Node {
        template <class... Args>
        explicit Node(std::uint64_t k, Args&&... args) : key(k), value(std::forward<Args>(args)...) {}
        std::uint64_t key;
        V value;
    };
    using List = std::list<Node>;
    void reset() {
        bucket_count_ = 8;
        mask_ = 7;
        buckets_.assign(bucket_count_ * 2, list_.end());
    }
    std::size_t bucketOf(std::uint64_t key) const {
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (int i = 0; i < 8; ++i) h = (h ^ ((key >> (8 * i)) & 0xff)) * 0x100000001b3ULL;
        return static_cast<std::size_t>(h & mask_);
    }
    typename List::iterator findIt(std::uint64_t key) {
        const auto bucket = bucketOf(key);
        const auto hi = buckets_[bucket * 2 + 1];
        if (hi == list_.end()) return list_.end();
        auto it = hi;
        for (;;) {
            if (it->key == key) return it;
            if (it == buckets_[bucket * 2]) return list_.end();
            --it;
        }
    }
    void rehash(std::size_t requested) {
        const std::size_t base = (requested - 1) | 1;
        std::size_t log = 0;
        while ((base >> (log + 1)) != 0) ++log;
        bucket_count_ = static_cast<std::size_t>(1) << (log + 1);
        mask_ = bucket_count_ - 1;
        buckets_.assign(bucket_count_ * 2, list_.end());
        for (auto node = list_.begin(); node != list_.end();) {
            const auto next = std::next(node);
            const auto bucket = bucketOf(node->key);
            if (buckets_[bucket * 2] == list_.end()) {
                buckets_[bucket * 2] = node;
                buckets_[bucket * 2 + 1] = node;
            } else {
                list_.splice(buckets_[bucket * 2], list_, node);
                buckets_[bucket * 2] = node;
            }
            node = next;
        }
    }

    List list_;
    std::vector<typename List::iterator> buckets_;
    std::size_t mask_ = 7;
    std::size_t bucket_count_ = 8;
};

} // namespace cm3d2::internal
