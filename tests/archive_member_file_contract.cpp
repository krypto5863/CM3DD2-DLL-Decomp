#include "file_factories.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

// Owned synthetic bytes only. These expectations follow the original factory
// and shared FileMemory methods, not behavior invented by a differential probe.
int main() {
    using cm3d2::internal::makeFileMemoryOwned;
    auto* bytes = new std::byte[6];
    std::memcpy(bytes, "abcdef", 6);
    std::unique_ptr<cm3d2::internal::IFile> file(makeFileMemoryOwned(bytes, 6));
    require(file->valid() && file->size() == 6 && file->tell() == 0, "initial owned state");
    require(file->data() == bytes, "factory must transfer rather than copy the buffer");
    char out[8]{};
    require(file->read(out, 2) == 2 && std::memcmp(out, "ab", 2) == 0, "read");
    require(file->readAt(out, 4, 5) == 2 && std::memcmp(out, "ef", 2) == 0, "positioned clamp");
    require(file->tell() == 2, "positioned read retains cursor");
    require(file->readAt(out, std::numeric_limits<std::uint64_t>::max(), 2) == 2 &&
        std::memcmp(out, "ab", 2) == 0, "positioned offset uses signed wrapped clamp");
    require(file->seek(std::numeric_limits<std::int64_t>::max(), true) && file->tell() == 6,
        "absolute upper clamp");
    require(file->seek(std::numeric_limits<std::int64_t>::max(), false) && file->tell() == 0,
        "relative addition wraps before signed clamp");
    require(file->seek(3, true) && file->tell() == 3, "position before close");
    file->close();
    require(!file->valid() && file->data() == nullptr, "closed pointers");
    require(file->size() == 6 && file->tell() == 3, "close retains scalar state");
    require(file->seek(1, true) && file->tell() == 3, "closed seek succeeds without moving");
    require(file->read(out, 1) == 0 && file->readAt(out, 0, 1) == 0, "closed reads");
    file->close(); // destructor then closes again: the owned buffer must be freed only once.

    std::unique_ptr<cm3d2::internal::IFile> empty(makeFileMemoryOwned(new std::byte[0], 0));
    require(empty->valid() && empty->data() != nullptr && empty->size() == 0, "empty owned file valid");
    require(empty->seek(42, true) && empty->tell() == 0 && empty->read(out, 1) == 0,
        "empty owned operations");
    std::unique_ptr<cm3d2::internal::IFile> invalid(makeFileMemoryOwned(nullptr, 0));
    require(!invalid->valid() && invalid->seek(42, true) && invalid->tell() == 0, "invalid seek");
}
