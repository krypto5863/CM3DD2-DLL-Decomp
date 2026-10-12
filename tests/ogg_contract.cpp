#include "cm3d2/ogg_api.h"
#include "cm3d2/file_api.h"
#include "cm3d2/file_system_delegate_api.h"
#include "cm3d2/file_system_api.h"
#include "../src/file_interface.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::vector<unsigned char> callback_bytes;
bool callback_called = false;
int provider_files_alive = 0;

class TrackedMemoryFile final : public cm3d2::internal::IFile {
public:
    explicit TrackedMemoryFile(CM3D2_FileHandle file) : file_(file) { ++provider_files_alive; }
    ~TrackedMemoryFile() override {
        DLL_FileMemory_DeleteileMemory(&file_);
        --provider_files_alive;
    }
    void close() override {}
    bool seek(std::int64_t offset, bool absolute) override { return DLL_File_SetSeek(&file_, offset, absolute); }
    std::uint64_t read(void* out, std::uint64_t size) override { return DLL_File_Read(&file_, out, size); }
    std::uint64_t readAt(void* out, std::uint64_t offset, std::uint64_t size) override {
        return static_cast<IFile*>(file_)->readAt(out, offset, size);
    }
    bool valid() const override { return static_cast<IFile*>(file_)->valid(); }
    const std::byte* data() const override { return static_cast<IFile*>(file_)->data(); }
    std::uint64_t tell() const override { return static_cast<IFile*>(file_)->tell(); }
    std::uint64_t size() const override { return static_cast<IFile*>(file_)->size(); }
private:
    CM3D2_FileHandle file_;
};

CM3D2_FileHandle CM3D2_CALL provideOgg(const wchar_t* name) {
    callback_called = true;
    if (std::wstring(name) != L"stream.ogg") return nullptr;
    CM3D2_FileHandle file = nullptr;
    DLL_FileMemory_CreateileMemory(&file);
    DLL_FileMemory_SetData(&file, callback_bytes.data(), static_cast<int32_t>(callback_bytes.size()));
    return new TrackedMemoryFile(DLL_FileMemory_GetInterfaceFilePointer(&file));
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

// Needs an Ogg Vorbis file from the user's game data. Set CM3D2_OGG_SAMPLE to its path;
// without it CTest reports a skip. Game audio is never committed.
int main() {
    const char* sample = std::getenv("CM3D2_OGG_SAMPLE");
    if (sample == nullptr || *sample == '\0') {
        std::printf("skipped: set CM3D2_OGG_SAMPLE to an Ogg Vorbis file\n");
        return 77;
    }

    CM3D2_SoundReaderOggHandle reader = nullptr;
    DLL_OGG_CreateSoundReaderOGG(nullptr, &reader);
    require(reader != nullptr, "creation returned null");
    require(DLL_OGG_IsOpenFile(&reader) == 0, "new reader should be closed");

    DLL_OGG_OpenFile(&reader, sample);
    require(DLL_OGG_IsOpenFile(&reader) == 1, "sample did not open");
    const uint16_t channels = DLL_OGG_GetChannels(&reader);
    const uint32_t rate = DLL_OGG_GetSampleRate(&reader);
    const uint32_t frames = DLL_OGG_GetTotalSampleLengs(&reader);
    require(channels == 1 || channels == 2, "unexpected channel count");
    require(rate > 0 && frames > 0, "empty stream metadata");
    require(DLL_OGG_GetSampleBit(&reader) == 16, "Ogg output must be 16-bit");

    // Read the whole stream: exactly frames * channels values, each within [-1, 1].
    const uint64_t expected = static_cast<uint64_t>(frames) * channels;
    std::vector<float> first(static_cast<std::size_t>(std::min<uint64_t>(expected, 256)));
    DLL_OGG_ReadUnity(&reader, first.data(), static_cast<uint32_t>(first.size()));
    DLL_OGG_SeekSample(&reader, 0, 1);
    std::vector<float> repeated(first.size());
    DLL_OGG_ReadUnity(&reader, repeated.data(), static_cast<uint32_t>(repeated.size()));
    require(first == repeated, "seek to start did not reproduce the initial block");
    DLL_OGG_SeekSample(&reader, 0, 1);
    std::vector<float> chunk(4096);
    uint64_t produced = 0;
    double peak = 0.0;
    while (produced < expected) {
        const uint32_t take = static_cast<uint32_t>(
            (expected - produced) < chunk.size() ? (expected - produced) : chunk.size());
        DLL_OGG_ReadUnity(&reader, chunk.data(), take);
        for (uint32_t i = 0; i < take; ++i) {
            require(std::fabs(chunk[i]) <= 1.0f + 1e-6f, "sample outside [-1, 1]");
            if (std::fabs(chunk[i]) > peak) peak = std::fabs(chunk[i]);
        }
        produced += take;
    }
    require(peak > 0.0, "stream decoded as silence");

    // Compare the saved initial block, not a value read after EOF.
    DLL_OGG_SeekSample(&reader, 0, 1);
    DLL_OGG_ReadUnity(&reader, repeated.data(), static_cast<uint32_t>(repeated.size()));
    require(first == repeated, "seek after EOF did not reproduce the initial block");

    DLL_OGG_CloseFile(&reader);
    require(DLL_OGG_IsOpenFile(&reader) == 0, "close should clear the open state");
    DLL_OGG_DeleteSoundReaderOGG(&reader);
    require(reader == nullptr, "delete should clear the handle");

    std::ifstream input(sample, std::ios::binary);
    callback_bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    require(!callback_bytes.empty(), "fixture bytes missing");
    CM3D2_FileSystemDelegateHandle delegate = nullptr;
    DLL_FileSystemDelegate_Create(&delegate);
    DLL_FileSystemDelegate_RegisterGetFileWideCallBack(&delegate, provideOgg);
    DLL_OGG_CreateSoundReaderOGG(
        DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(&delegate), &reader);
    DLL_OGG_OpenFile(&reader, "stream.ogg");
    require(callback_called && DLL_OGG_IsOpenFile(&reader), "delegate-backed Ogg did not open");
    require(provider_files_alive == 1, "provider file released before reader close");
    require(DLL_OGG_GetChannels(&reader) == channels && DLL_OGG_GetTotalSampleLengs(&reader) == frames,
        "delegate metadata differs");
    DLL_OGG_ReadUnity(&reader, repeated.data(), static_cast<uint32_t>(repeated.size()));
    require(first == repeated, "delegate PCM differs from disk PCM");
    DLL_OGG_OpenFile(&reader, sample);
    require(!DLL_OGG_IsOpenFile(&reader), "failed provider silently fell back to disk");
    require(provider_files_alive == 0, "reopen did not release prior provider file");
    DLL_OGG_OpenFile(&reader, "stream.ogg");
    require(DLL_OGG_IsOpenFile(&reader) && provider_files_alive == 1, "delegate could not reopen");
    DLL_OGG_CloseFile(&reader);
    require(provider_files_alive == 0, "close leaked provider file");
    DLL_OGG_OpenFile(&reader, "stream.ogg");
    DLL_OGG_DeleteSoundReaderOGG(&reader);
    require(provider_files_alive == 0, "delete leaked provider file");
    DLL_FileSystemDelegate_Delete(&delegate);

    CM3D2_FileSystemHandle fs{};
    DLL_FileSystem_CreateFileSystemWindows(&fs);
    const auto path = std::filesystem::u8path(sample);
    require(DLL_FileSystem_AddFolder(&fs, path.parent_path().u8string().c_str()), "sample folder did not register");
    DLL_OGG_CreateSoundReaderOGG(DLL_FileSystem_GetInterfaceFileSystemWidePointer(&fs), &reader);
    DLL_OGG_OpenFile(&reader, path.filename().u8string().c_str());
    require(DLL_OGG_IsOpenFile(&reader), "Windows filesystem-backed Ogg did not open");
    DLL_OGG_ReadUnity(&reader, repeated.data(), static_cast<uint32_t>(repeated.size()));
    require(first == repeated, "filesystem PCM differs from disk PCM");
    DLL_OGG_DeleteSoundReaderOGG(&reader);
    DLL_FileSystem_DeleteFileSystem(&fs);
    std::printf("ok: channels=%u rate=%u frames=%u peak=%.4f\n", channels, rate, frames, peak);
    return 0;
}
