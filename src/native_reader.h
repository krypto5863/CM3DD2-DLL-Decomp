#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cm3d2::internal {

// Bounded reader for the native serialized formats (menu, anm). The natives
// read unchecked; every refusal here is deliberate hardening.
class NativeReader {
public:
    NativeReader(const std::uint8_t* bytes, std::size_t size) : bytes_(bytes), size_(size) {}

    bool byte(std::uint8_t& value) {
        if (position_ == size_) return false;
        value = bytes_[position_++];
        return true;
    }

    bool word(std::uint32_t& value) {
        if (size_ - position_ < 4) return false;
        value = static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3]) << 24);
        position_ += 4;
        return true;
    }

    // Returns a pointer to `count` bytes and advances, or nullptr when short.
    const std::uint8_t* take(std::size_t count) {
        if (count > size_ - position_) return nullptr;
        const std::uint8_t* start = bytes_ + position_;
        position_ += count;
        return start;
    }

    bool string(std::string& value) {
        std::uint8_t first = 0;
        if (!byte(first)) return false;
        std::size_t length = first;
        if (first > 0x7f) {
            std::uint8_t second = 0;
            if (!byte(second)) return false;
            // Menu helper FUN_18010c450 and the anm parser FUN_180065b60 both
            // consume at most two bytes: first + max(second - 1, 0) * 128.
            // Do not substitute an unrestricted .NET 7-bit integer reader.
            if (second > 1) length += static_cast<std::size_t>(second) * 128 - 128;
        }
        const std::uint8_t* start = take(length);
        if (start == nullptr) return false;
        value.assign(reinterpret_cast<const char*>(start), length);
        return true;
    }

    std::size_t position() const { return position_; }

private:
    const std::uint8_t* bytes_;
    std::size_t size_;
    std::size_t position_ = 0;
};

} // namespace cm3d2::internal
