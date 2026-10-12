#pragma once

#include <cstddef>
#include <cstdint>

namespace cm3d2::internal {

/*
 * Semantic reconstruction of the first nine shared vtable slots. The native
 * binary has a deleting destructor in slot 0, followed by close, seek, read,
 * positioned-read, validity, data pointer, tell, and size. Native consumers
 * (AnmParse_Open at 0x180067430) call slot 8 (+0x40) for the size and slot 4
 * (+0x20) as readAt(buffer, 0, size). The interface file pointer exchanged
 * through the C API (e.g. DLL_FileMemory_GetInterfaceFilePointer) is a pointer
 * to an object of this type.
 */
class IFile {
public:
    virtual ~IFile() = default;
    virtual void close() = 0;
    virtual bool seek(std::int64_t offset, bool absolute) = 0;
    virtual std::uint64_t read(void* destination, std::uint64_t count) = 0;
    virtual std::uint64_t readAt(
        void* destination,
        std::uint64_t offset,
        std::uint64_t count) = 0;
    virtual bool valid() const = 0;
    virtual const std::byte* data() const = 0;
    virtual std::uint64_t tell() const = 0;
    virtual std::uint64_t size() const = 0;
};

} // namespace cm3d2::internal
