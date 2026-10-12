#include "msvc_ordered_map.h"

#include <algorithm>
#include <iterator>

namespace cm3d2::internal {

MsvcOrderedNameMap::MsvcOrderedNameMap() {
    // FUN_18006e780 fills 16 bucket slots; the container starts with 8 buckets.
    bucket_count_ = 8;
    mask_ = 7;
    buckets_.assign(bucket_count_ * 2, list_.end());
}

void MsvcOrderedNameMap::clear() {
    list_.clear();
    size_ = 0;
    std::fill(buckets_.begin(), buckets_.end(), list_.end());
}

std::size_t MsvcOrderedNameMap::bucketOf(std::uint64_t key) const {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 8; ++i) {
        h = (h ^ ((key >> (8 * i)) & 0xff)) * 0x100000001b3ULL;
    }
    return static_cast<std::size_t>(h & mask_);
}

const std::wstring* MsvcOrderedNameMap::find(std::uint64_t key) const {
    const auto bucket = bucketOf(key);
    const auto hi = buckets_[bucket * 2 + 1];
    if (hi == list_.end()) {
        return nullptr;
    }
    auto it = hi;
    for (;;) {
        if (it->key == key) {
            return &it->value;
        }
        if (it == buckets_[bucket * 2]) {
            return nullptr;
        }
        --it;
    }
}

void MsvcOrderedNameMap::tryEmplace(std::uint64_t key, const std::wstring& value) {
    if (find(key) != nullptr) {
        return;
    }
    // FUN_180087880: grow when (size + 1) / buckets exceeds max_load_factor (1.0).
    if (static_cast<double>(size_ + 1) / static_cast<double>(bucket_count_) > 1.0) {
        const std::size_t desired = std::max<std::size_t>(8, size_ + 1);
        std::size_t target = bucket_count_;
        if (bucket_count_ < desired) {
            if (bucket_count_ > 0x1ff) {
                target = desired;
            } else {
                target = std::max(bucket_count_ * 8, desired);
            }
        }
        rehash(target);
    }
    const auto bucket = bucketOf(key);
    // The new node goes in front of the first node of its bucket, or at the end of the list
    // when the bucket is empty.
    const bool empty = buckets_[bucket * 2] == list_.end();
    const auto where = empty ? list_.end() : buckets_[bucket * 2];
    const auto inserted = list_.insert(where, Node{key, value});
    ++size_;
    buckets_[bucket * 2] = inserted;
    if (empty) {
        buckets_[bucket * 2 + 1] = inserted;
    }
}

// FUN_180075c70: every node is re-bucketed in list order; a node that joins a non-empty bucket
// is moved in front of that bucket's first node.
void MsvcOrderedNameMap::rehash(std::size_t requested) {
    const std::size_t base = (requested - 1) | 1;
    std::size_t log = 0;
    while ((base >> (log + 1)) != 0) {
        ++log;
    }
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

std::vector<std::pair<std::uint64_t, const std::wstring*>> MsvcOrderedNameMap::entries() const {
    std::vector<std::pair<std::uint64_t, const std::wstring*>> out;
    out.reserve(size_);
    for (const auto& node : list_) {
        out.emplace_back(node.key, &node.value);
    }
    return out;
}

std::vector<const std::wstring*> MsvcOrderedNameMap::values() const {
    std::vector<const std::wstring*> out;
    out.reserve(size_);
    for (const auto& node : list_) {
        out.push_back(&node.value);
    }
    return out;
}

} // namespace cm3d2::internal

namespace cm3d2::internal {

MsvcOrderedStringSet::MsvcOrderedStringSet() {
    bucket_count_ = 8;
    mask_ = 7;
    buckets_.assign(bucket_count_ * 2, list_.end());
}

std::uint64_t MsvcOrderedStringSet::hashOf(const std::wstring& value) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    for (std::size_t i = 0; i < value.size() * 2; ++i) {
        h = (h ^ bytes[i]) * 0x100000001b3ULL;
    }
    return h;
}

bool MsvcOrderedStringSet::insert(const std::wstring& value) {
    const std::uint64_t hash = hashOf(value);
    auto bucket = static_cast<std::size_t>(hash & mask_);
    const auto hi = buckets_[bucket * 2 + 1];
    if (hi != list_.end()) {
        for (auto it = hi;; --it) {
            if (*it == value) {
                return false;
            }
            if (it == buckets_[bucket * 2]) {
                break;
            }
        }
    }
    if (static_cast<double>(list_.size() + 1) / static_cast<double>(bucket_count_) > 1.0) {
        const std::size_t desired = std::max<std::size_t>(8, list_.size() + 1);
        std::size_t target = bucket_count_;
        if (bucket_count_ < desired) {
            target = bucket_count_ > 0x1ff ? desired : std::max(bucket_count_ * 8, desired);
        }
        rehash(target);
        bucket = static_cast<std::size_t>(hash & mask_);
    }
    const bool empty = buckets_[bucket * 2] == list_.end();
    const auto where = empty ? list_.end() : buckets_[bucket * 2];
    const auto inserted = list_.insert(where, value);
    buckets_[bucket * 2] = inserted;
    if (empty) {
        buckets_[bucket * 2 + 1] = inserted;
    }
    return true;
}

void MsvcOrderedStringSet::rehash(std::size_t requested) {
    const std::size_t base = (requested - 1) | 1;
    std::size_t log = 0;
    while ((base >> (log + 1)) != 0) {
        ++log;
    }
    bucket_count_ = static_cast<std::size_t>(1) << (log + 1);
    mask_ = bucket_count_ - 1;
    buckets_.assign(bucket_count_ * 2, list_.end());
    for (auto node = list_.begin(); node != list_.end();) {
        const auto next = std::next(node);
        const auto bucket = static_cast<std::size_t>(hashOf(*node) & mask_);
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

} // namespace cm3d2::internal
