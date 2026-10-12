#include "cm3d2/file_api.h"
#include "file_factories.h"
#include "file_api_internal.h"
#include "file_interface.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <string>
#include <limits>
#include <new>
#include <memory>

namespace {

using byte = std::byte;

using cm3d2::internal::IFile;

class FileMemory final : public IFile {
public:
    ~FileMemory() override { close(); }

    void close() override {
        delete[] owned_;
        owned_ = nullptr;
        data_ = nullptr;
        cursor_ = nullptr;
        // FUN_180069a00 clears only the three pointers. Tell/size survive close.
    }

    bool seek(std::int64_t offset, bool absolute) override {
        // FUN_180069d80 uses wrapping ADD followed by signed JL/CMOVL, not
        // saturating unsigned arithmetic. Invalid files retain their position.
        if (valid()) {
            position_ = clampPosition((absolute ? begin_ : position_) +
                static_cast<std::uint64_t>(offset));
        }
        cursor_ = pointerAt(position_);
        // FUN_180069ba0 returns true even on a closed/invalid FileMemory.
        return true;
    }

    std::uint64_t read(void* destination, std::uint64_t count) override {
        // Native memcpy has no null-destination check; keep that unsupported
        // crash input normalized here, rather than intentionally faulting.
        const auto upper = begin_ + size_;
        if (!valid() || destination == nullptr || asSigned(upper) < asSigned(position_)) {
            return 0;
        }
        if (asSigned(upper) < asSigned(position_ + count)) {
            count = upper - position_;
        }
        // A sign-wrapping count bypasses the native comparison and would copy
        // outside the allocation. Retain bounded copying for that unsupported
        // crash input; ordinary counts follow the recovered signed branch.
        count = std::min(count, upper - position_);
        copyBytes(destination, cursor_, count);
        seek(asSigned(count), false);
        return count;
    }

    std::uint64_t readAt(
        void* destination,
        std::uint64_t offset,
        std::uint64_t count) override {
        if (!valid() || destination == nullptr) {
            return 0;
        }

        const auto absolute = clampPosition(begin_ + offset);
        const auto upper = begin_ + size_;
        if (asSigned(upper) < asSigned(absolute)) {
            return 0;
        }
        if (asSigned(upper) < asSigned(absolute + count)) {
            count = upper - absolute;
        }
        count = std::min(count, upper - absolute); // same unsupported-count normalization
        copyBytes(destination, pointerAt(absolute), count);
        return count;
    }

    bool valid() const override { return data_ != nullptr; }
    const byte* data() const override { return pointerAt(begin_); }
    std::uint64_t tell() const override { return position_ - begin_; }
    std::uint64_t size() const override { return size_; }

    /* FileMemory-only vtable slots 9 through 12. */
    virtual void setDataOwned(byte* data, std::uint64_t total, std::uint64_t begin) {
        close();
        owned_ = data;
        // FUN_180069a30 transfers ownership without a second allocation/copy.
        data_ = data;
        begin_ = begin;
        size_ = total - begin;
        cursor_ = data;
        seek(0, true);
    }

    virtual void setDataCopy(const void* data, std::uint64_t total, std::uint64_t begin) {
        close();
        if (data == nullptr && total != 0) {
            return;
        }

        // FUN_180069aa0 allocates even for zero bytes: an empty copied file is valid.
        auto* copy = new byte[total];
        if (total != 0) {
            std::memset(copy, 0, static_cast<std::size_t>(total));
            std::memcpy(copy, data, static_cast<std::size_t>(total));
        }
        owned_ = copy;
        configure(copy, total, begin);
    }

    virtual void setDataBorrowed(const void* data, std::uint64_t total, std::uint64_t begin) {
        close();
        configure(static_cast<const byte*>(data), total, begin);
    }

    virtual std::uint64_t copyBytes(void* destination, const void* source, std::uint64_t count) {
        if (count == 0) {
            return 0;
        }
        std::memcpy(destination, source, static_cast<std::size_t>(count));
        return count;
    }

private:
    static std::int64_t asSigned(std::uint64_t value) {
        std::int64_t result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    std::uint64_t clampPosition(std::uint64_t candidate) const {
        const auto upper = begin_ + size_;
        if (asSigned(upper) < asSigned(candidate)) {
            return upper;
        }
        return asSigned(candidate) < asSigned(begin_) ? begin_ : candidate;
    }

    const byte* pointerAt(std::uint64_t offset) const {
        // Integer address addition also represents native closed-object state
        // without performing C++ pointer arithmetic on a null pointer.
        return reinterpret_cast<const byte*>(reinterpret_cast<std::uintptr_t>(data_) + offset);
    }

    void configure(const byte* data, std::uint64_t total, std::uint64_t begin) {
        data_ = data;
        begin_ = std::min(begin, total);
        size_ = total - begin_;
        position_ = begin_;
        cursor_ = data_ == nullptr ? nullptr : data_ + position_;
    }

    byte* owned_ = nullptr;          // +0x08
    const byte* data_ = nullptr;     // +0x10
    const byte* cursor_ = nullptr;   // +0x18
    std::uint64_t begin_ = 0;        // +0x20
    std::uint64_t position_ = 0;     // +0x28
    std::uint64_t size_ = 0;         // +0x30
};

class Win32File final : public IFile {
public:
    ~Win32File() override { close(); }

    // FUN_1800745b0: any non-null handle (INVALID_HANDLE_VALUE included) is passed to CloseHandle.
    void close() override {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
        size_ = 0;
        position_ = 0;
    }

    // FUN_180074720: signed clamp to the file, SetFilePointerEx, the result is stored without a success check and the call
    // always reports true (even for a closed object, whose SetFilePointerEx fails; the native then stores an uninitialized
    // position, here the position is kept).
    bool seek(std::int64_t offset, bool absolute) override {
        const auto current = static_cast<std::int64_t>(position_);
        const auto end = static_cast<std::int64_t>(size_);
        const std::int64_t lower = absolute ? 0 : -current;
        const std::int64_t upper = absolute ? end : end - current;
        if (upper < offset) {
            offset = upper;
        }
        if (offset < lower) {
            offset = lower;
        }
        LARGE_INTEGER distance{};
        LARGE_INTEGER result{};
        result.QuadPart = static_cast<LONGLONG>(position_);
        distance.QuadPart = offset;
        if (SetFilePointerEx(handle_, distance, &result, absolute ? FILE_BEGIN : FILE_CURRENT)) {
            position_ = static_cast<std::uint64_t>(result.QuadPart);
        }
        return true;
    }

    // FUN_1800745f0: the remaining-bytes clamp compares signed values; the request is clamped to 32 bits.
    std::uint64_t read(void* destination, std::uint64_t count) override {
        if (!valid() || destination == nullptr) {
            return 0;
        }
        if (static_cast<std::int64_t>(size_ - position_) < static_cast<std::int64_t>(count)) {
            count = size_ - position_;
        }
        const DWORD request = count > 0xffffffffULL ? 0xffffffffUL : static_cast<DWORD>(count);
        DWORD completed = 0;
        ReadFile(handle_, destination, request, &completed, nullptr);
        position_ += completed;
        return completed;
    }

    // FUN_180074680: signed arithmetic, the count is truncated to 32 bits (no clamp), failures of SetFilePointerEx are ignored
    // (a negative offset leaves the pointer where it was, so the read starts at the current position).
    std::uint64_t readAt(
        void* destination,
        std::uint64_t offset,
        std::uint64_t count) override {
        if (!valid() || destination == nullptr) {
            return 0;
        }
        if (static_cast<std::int64_t>(size_) - static_cast<std::int64_t>(offset) < static_cast<std::int64_t>(count)) {
            count = static_cast<std::uint64_t>(static_cast<std::int64_t>(size_) - static_cast<std::int64_t>(offset));
        }
        LARGE_INTEGER target{};
        target.QuadPart = static_cast<LONGLONG>(offset);
        SetFilePointerEx(handle_, target, nullptr, FILE_BEGIN);
        DWORD completed = 0;
        ReadFile(handle_, destination, static_cast<DWORD>(count), &completed, nullptr);
        target.QuadPart = static_cast<LONGLONG>(position_);
        SetFilePointerEx(handle_, target, nullptr, FILE_BEGIN);
        return completed;
    }

    // FUN_180069ee0: only a null handle is invalid; INVALID_HANDLE_VALUE counts as valid.
    bool valid() const override { return handle_ != nullptr; }
    const byte* data() const override { return nullptr; }
    std::uint64_t tell() const override { return position_; }
    std::uint64_t size() const override { return size_; }

    /* Win32File-only vtable slots 9 through 11. */
    virtual bool openWide(const wchar_t* path) { return open(path, CreateFileW); }
    virtual bool openAnsi(const char* path) { return open(path, CreateFileA); }
    virtual bool atEnd() const { return size_ == position_; }

private:
    // FUN_180074520 / FUN_180074490: the handle is stored as returned (a failed open stores INVALID_HANDLE_VALUE, which the object then
    // counts as valid) and the call succeeds whenever the handle is not null; the size comes from GetFileSizeEx without checking
    // it (the native keeps an uninitialized value when that fails; zero here).
    template <typename Char, typename Open>
    bool open(const Char* path, Open openFunction) {
        close();
        if (path == nullptr) {
            return false;
        }
        handle_ = openFunction(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_READONLY,
            nullptr);
        if (handle_ == nullptr) {
            return false;
        }
        LARGE_INTEGER fileSize{};
        GetFileSizeEx(handle_, &fileSize);
        size_ = static_cast<std::uint64_t>(fileSize.QuadPart);
        return true;
    }

    HANDLE handle_ = nullptr;       // +0x08
    std::uint64_t size_ = 0;        // +0x10
    std::uint64_t position_ = 0;    // +0x18
};


// FileNormalWindows: vtable 0x1801dcf50, 0x28 bytes. Slots 1-8 follow the shared IFile order
// (close FUN_180074780, seek FUN_180074a80 via FUN_180074c70, read FUN_180074ab0, readAt
// FUN_180074b60, valid FUN_180069ee0, data FUN_180069ef0, tell FUN_180075280, size DAT 0x20).
class FileNormalWindows final : public IFile {
public:
    ~FileNormalWindows() override { close(); }

    void close() override {
        if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
        size_ = 0;
        position_ = 0;
    }

    bool seek(std::int64_t offset, bool absolute) override {
        const auto target = clampedTarget(offset, absolute);
        position_ = target;
        if (file_ != nullptr) {
            _fseeki64(file_, target, SEEK_SET);
        }
        return true;
    }

    std::uint64_t read(void* destination, std::uint64_t count) override {
        if (!valid() || destination == nullptr || std::feof(file_) != 0) {
            return 0;
        }
        fpos_t saved{};
        if (std::fgetpos(file_, &saved) != 0) {
            return 0;
        }
        count = std::min(count, static_cast<std::uint64_t>(size_ - position_));
        const auto completed = readLow(destination, count);
        position_ += completed;
        _fseeki64(file_, static_cast<std::int64_t>(position_), SEEK_SET);
        return completed;
    }

    std::uint64_t readAt(void* destination, std::uint64_t offset, std::uint64_t count) override {
        if (!valid() || destination == nullptr) {
            return 0;
        }
        fpos_t saved{};
        if (std::fgetpos(file_, &saved) != 0) {
            return 0;
        }
        std::int64_t target = static_cast<std::int64_t>(offset) + static_cast<std::int64_t>(begin_);
        const auto upper = static_cast<std::int64_t>(begin_ + size_);
        if (target > upper) {
            target = upper;
        } else if (target < static_cast<std::int64_t>(begin_)) {
            target = static_cast<std::int64_t>(begin_);
        }
        _fseeki64(file_, target, SEEK_SET);
        std::uint64_t completed = 0;
        if (std::feof(file_) == 0) {
            count = std::min(count, static_cast<std::uint64_t>(static_cast<std::int64_t>(size_) - target));
            completed = readLow(destination, count);
        }
        std::fsetpos(file_, &saved);
        return completed;
    }

    bool valid() const override { return file_ != nullptr; }
    const byte* data() const override { return nullptr; }
    std::uint64_t tell() const override { return position_ - begin_; }
    std::uint64_t size() const override { return size_; }

    // Slot 10 (FUN_180074880): open a wide path.
    bool openWide(const wchar_t* path) {
        close();
        if (path == nullptr || _wfopen_s(&file_, path, L"rb") != 0 || file_ == nullptr) {
            file_ = nullptr;
            return false;
        }
        const auto length = _filelengthi64(_fileno(file_));
        if (length < 0) {
            std::fclose(file_);
            file_ = nullptr;
            return false;
        }
        begin_ = 0;
        size_ = static_cast<std::uint64_t>(length) - begin_;
        position_ = begin_;
        seek(0, true);
        return true;
    }

private:
    // FUN_180074c70: the position a seek lands on, clamped to the file.
    std::uint64_t clampedTarget(std::int64_t offset, bool absolute) const {
        std::int64_t current = static_cast<std::int64_t>(position_);
        std::int64_t result = current;
        if (absolute) {
            offset += static_cast<std::int64_t>(begin_);
        }
        if (valid()) {
            if (!absolute) {
                offset = current + offset;
            }
            const auto lower = static_cast<std::int64_t>(begin_);
            const auto upper = static_cast<std::int64_t>(size_ + begin_);
            result = upper;
            if (offset <= upper) {
                result = offset < lower ? lower : offset;
            }
        }
        return static_cast<std::uint64_t>(result);
    }

    // Slot 12 (FUN_180074d00): one fread of the whole block; a short read reports the bytes seen.
    std::uint64_t readLow(void* destination, std::uint64_t count) {
        fpos_t before{};
        if (std::fgetpos(file_, &before) != 0) {
            return 0;
        }
        std::fread(destination, static_cast<std::size_t>(count), 1, file_);
        if (std::feof(file_) != 0) {
            fpos_t after{};
            std::fgetpos(file_, &after);
            return static_cast<std::uint64_t>(after - before);
        }
        return count;
    }

    FILE* file_ = nullptr;           // +0x08
    std::uint64_t begin_ = 0;        // +0x10
    std::uint64_t position_ = 0;     // +0x18
    std::uint64_t size_ = 0;         // +0x20
};

static_assert(sizeof(FileMemory) == 0x38, "recovered FileMemory layout changed");
static_assert(sizeof(Win32File) == 0x20, "recovered Win32File layout changed");
static_assert(sizeof(FileNormalWindows) == 0x28, "recovered FileNormalWindows layout changed");

std::atomic<std::int32_t> g_fileInstanceCount{0};

IFile* getFile(CM3D2_FileHandle* file) {
    return file == nullptr ? nullptr : static_cast<IFile*>(*file);
}

} // namespace

namespace cm3d2::internal {

IFile* openFileNormalWindows(const std::wstring& path) {
    auto* file = new FileNormalWindows();
    if (!file->openWide(path.c_str())) {
        delete file;
        return nullptr;
    }
    return file;
}

IFile* makeFileMemoryOwned(std::byte* data, std::uint64_t size) {
    // Allocation failure must not leak the transferred buffer. No instance
    // count here: FUN_18008feb0 leaves counting to the exported GetFile wrapper.
    std::unique_ptr<std::byte[]> pending(data);
    auto file = std::make_unique<FileMemory>();
    file->setDataOwned(pending.release(), size, 0);
    return file.release();
}

void noteFileInstanceCreated() {
    g_fileInstanceCount.fetch_add(1, std::memory_order_relaxed);
}

const std::byte* fileData(CM3D2_FileHandle file) {
    const auto* object = static_cast<const IFile*>(file);
    return object == nullptr ? nullptr : object->data();
}

} // namespace cm3d2::internal

extern "C" {

void CM3D2_CALL DLL_Win32File_Create(CM3D2_FileHandle* out_file) {
    if (out_file != nullptr) {
        *out_file = new Win32File();
    }
}

void CM3D2_CALL DLL_Win32File_Delete(CM3D2_FileHandle* file) {
    auto* object = getFile(file);
    if (object != nullptr) {
        delete object;
    }
    if (file != nullptr) {
        *file = nullptr;
    }
}

uint32_t CM3D2_CALL DLL_Win32File_Open(
    CM3D2_FileHandle* file,
    const wchar_t* path) {
    auto* object = static_cast<Win32File*>(getFile(file));
    return object != nullptr && object->openWide(path);
}

uint32_t CM3D2_CALL DLL_File_IsValid(CM3D2_FileHandle* file) {
    const auto* object = getFile(file);
    return object != nullptr && object->valid();
}

uint32_t CM3D2_CALL DLL_File_SetSeek(
    CM3D2_FileHandle* file,
    int64_t offset,
    uint8_t absolute) {
    auto* object = getFile(file);
    return object != nullptr && object->seek(offset, absolute != 0);
}

uint64_t CM3D2_CALL DLL_File_Read(
    CM3D2_FileHandle* file,
    void* destination,
    uint64_t byte_count) {
    auto* object = getFile(file);
    return object == nullptr ? 0 : object->read(destination, byte_count);
}

uint64_t CM3D2_CALL DLL_File_GetTell(CM3D2_FileHandle* file) {
    const auto* object = getFile(file);
    return object == nullptr ? 0 : object->tell();
}

uint64_t CM3D2_CALL DLL_File_GetSize(CM3D2_FileHandle* file) {
    const auto* object = getFile(file);
    return object == nullptr ? 0 : object->size();
}

void CM3D2_CALL DLL_File_CloseFile(CM3D2_FileHandle* file) {
    auto* object = getFile(file);
    if (object != nullptr) {
        delete object;
        g_fileInstanceCount.fetch_sub(1, std::memory_order_relaxed);
    }
}

void CM3D2_CALL DLL_File_ReleaseInstanceCount(void) {
    g_fileInstanceCount.fetch_sub(1, std::memory_order_relaxed);
}

void CM3D2_CALL DLL_FileMemory_CreateileMemory(CM3D2_FileHandle* out_file) {
    if (out_file != nullptr) {
        *out_file = new FileMemory();
        g_fileInstanceCount.fetch_add(1, std::memory_order_relaxed);
    }
}

CM3D2_FileHandle CM3D2_CALL DLL_FileMemory_GetInterfaceFilePointer(
    CM3D2_FileHandle* file) {
    return file == nullptr ? nullptr : *file;
}

void CM3D2_CALL DLL_FileMemory_SetData(
    CM3D2_FileHandle* file,
    const void* data,
    int32_t byte_count) {
    auto* object = static_cast<FileMemory*>(getFile(file));
    if (object != nullptr) {
        if (byte_count < 0) {
            object->close(); // Deliberate normalization: do not allocate a sign-extended length.
        } else {
            object->setDataCopy(data, static_cast<std::uint64_t>(byte_count), 0);
        }
    }
}

void CM3D2_CALL DLL_FileMemory_DeleteileMemory(CM3D2_FileHandle* file) {
    auto* object = getFile(file);
    if (object != nullptr) {
        delete object;
    }
}

} // extern "C"
