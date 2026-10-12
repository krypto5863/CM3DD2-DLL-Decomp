#include "cm3d2/ogg_api.h"
#include "file_system_facade.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <memory>
#include <limits>
#include <string>
#include <vector>

// Declarations only. The implementation is compiled as C in src/stb_vorbis_impl.c.
// stb_vorbis is dual-licensed MIT or public domain; see third_party/stb/stb_vorbis.c.
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb/stb_vorbis.c"

namespace {

// Semantic reconstruction of the native reader. The native keeps the decoder at
// +0x20, the channel count at +0x32, the rate at +0x34, bits at +0x3e (always 16
// for Ogg), the total samples at +0x50, and the byte position at +0x48.
struct SoundReaderOgg {
    void* file_system = nullptr;
    std::unique_ptr<cm3d2::internal::IFile> source_file;
    std::vector<unsigned char> file;
    stb_vorbis* decoder = nullptr;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits = 16;
    std::uint32_t total_frames = 0;
    std::uint32_t position_frames = 0;
    std::uint64_t consumed_samples = 0;
    std::vector<short> fifo;      // decoded 16-bit interleaved PCM not yet consumed
    std::size_t fifo_offset = 0;  // consumed prefix of fifo
};

SoundReaderOgg* reader(CM3D2_SoundReaderOggHandle* handle) {
    return handle == nullptr ? nullptr : static_cast<SoundReaderOgg*>(*handle);
}

void releaseDecoder(SoundReaderOgg& self) {
    if (self.decoder != nullptr) {
        stb_vorbis_close(self.decoder);
        self.decoder = nullptr;
    }
    self.file.clear();
    self.source_file.reset();
    self.channels = 0;
    self.sample_rate = 0;
    self.total_frames = 0;
    self.position_frames = 0;
    self.consumed_samples = 0;
    self.fifo.clear();
    self.fifo_offset = 0;
}

bool readWholeFile(const char* utf8_path, const void* file_system, std::vector<unsigned char>& out,
    std::unique_ptr<cm3d2::internal::IFile>& source_file) {
    if (utf8_path == nullptr) return false;
    const int wide_size = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, nullptr, 0);
    if (wide_size <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wide.data(), wide_size);
    wide.resize(static_cast<std::size_t>(wide_size) - 1);

    if (file_system != nullptr) {
        std::unique_ptr<cm3d2::internal::IFile> file(
            cm3d2::internal::fileSystemOpenWide(file_system, wide));
        if (!file) return false;
        const auto size = file->size();
        if (size == 0 || size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return false;
        out.resize(static_cast<std::size_t>(size));
        if (const auto* data = file->data()) {
            std::memcpy(out.data(), data, out.size());
        } else if (file->readAt(out.data(), 0, size) != size) {
            return false;
        }
        // Native reader +0x20 owns the provider file until close/reopen/delete.
        source_file = std::move(file);
        return true;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, wide.c_str(), L"rb") != 0 || file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    return got == out.size();
}

// Pulls one decoded block into the FIFO. Returns false at end of stream.
bool fillBlock(SoundReaderOgg& self) {
    if (self.decoder == nullptr) return false;
    const int channels = self.channels;
    std::vector<short> block(static_cast<std::size_t>(4096) * channels);
    const int frames = stb_vorbis_get_samples_short_interleaved(
        self.decoder, channels, block.data(), static_cast<int>(block.size()));
    if (frames <= 0) return false;
    self.fifo.erase(self.fifo.begin(), self.fifo.begin() + static_cast<std::ptrdiff_t>(self.fifo_offset));
    self.fifo_offset = 0;
    self.fifo.insert(self.fifo.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(frames) * channels);
    return true;
}

} // namespace

extern "C" {

// The native stores the file system pointer at +0x18 and writes the new reader
// to *out (0x58 bytes plus a 0x348-byte stream block).
void CM3D2_CALL DLL_OGG_CreateSoundReaderOGG(void* file_system, CM3D2_SoundReaderOggHandle* out_reader) {
    if (out_reader == nullptr) return;
    auto* self = new (std::nothrow) SoundReaderOgg;
    if (self != nullptr) self->file_system = file_system;
    *out_reader = self;
}

void CM3D2_CALL DLL_OGG_DeleteSoundReaderOGG(CM3D2_SoundReaderOggHandle* handle) {
    if (handle == nullptr) return;
    if (auto* self = reader(handle)) {
        releaseDecoder(*self);
        delete self;
    }
    *handle = nullptr;
}

// Native FUN_180094720: release any previous stream, open the new one, and read
// channels, rate, and total length from the stream. Bits are always 16.
// Uses the supplied reconstructed wide file-system interface, including delegates.
// Deliberate normalization: a null interface permits direct disk reads.
// Failure leaves the reader closed; a supplied provider never falls back to disk.
void CM3D2_CALL DLL_OGG_OpenFile(CM3D2_SoundReaderOggHandle* handle, const char* utf8_path) {
    auto* self = reader(handle);
    if (self == nullptr) return;
    releaseDecoder(*self);

    if (!readWholeFile(utf8_path, self->file_system, self->file, self->source_file)) return;
    int error = 0;
    self->decoder = stb_vorbis_open_memory(self->file.data(), static_cast<int>(self->file.size()), &error, nullptr);
    if (self->decoder == nullptr) {
        releaseDecoder(*self);
        return;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(self->decoder);
    self->channels = static_cast<std::uint16_t>(info.channels);
    self->sample_rate = info.sample_rate;
    self->total_frames = stb_vorbis_stream_length_in_samples(self->decoder);
    self->bits = 16;
}

uint32_t CM3D2_CALL DLL_OGG_IsOpenFile(CM3D2_SoundReaderOggHandle* handle) {
    const auto* self = reader(handle);
    return self != nullptr && self->decoder != nullptr ? 1 : 0;
}

void CM3D2_CALL DLL_OGG_CloseFile(CM3D2_SoundReaderOggHandle* handle) {
    if (auto* self = reader(handle)) releaseDecoder(*self);
}

// Native FUN_1800948e0 fills an internal block. Here it decodes the next block
// into the FIFO without consuming it; ReadUnity consumes the FIFO.
uint32_t CM3D2_CALL DLL_OGG_Read(CM3D2_SoundReaderOggHandle* handle, void* destination, uint32_t read_size) {
    // The original (0x180072dc0) is "mov rcx,[rcx]; jmp FUN_1800948e0": the block reader that ReadUnity also uses, called
    // with the caller's buffer and byte count (the game never calls this export itself; the managed declaration takes
    // dest and read_size). Raw 16-bit interleaved samples are copied out of the same FIFO ReadUnity consumes.
    auto* self = reader(handle);
    if (self == nullptr || destination == nullptr) return 0;
    auto* out = static_cast<unsigned char*>(destination);
    uint32_t written = 0;
    while (written + sizeof(short) <= read_size) {
        if (self->fifo_offset >= self->fifo.size() && !fillBlock(*self)) break;
        const short sample = self->fifo[self->fifo_offset++];
        std::memcpy(out + written, &sample, sizeof(sample));
        written += sizeof(sample);
    }
    self->consumed_samples += written / sizeof(short);
    self->position_frames = self->channels == 0
        ? 0
        : static_cast<std::uint32_t>(self->consumed_samples / self->channels);
    return written;
}

// Native converts 16-bit samples to float by dividing by 32767. Missing samples at
// end of stream are written as 0 here.
void CM3D2_CALL DLL_OGG_ReadUnity(CM3D2_SoundReaderOggHandle* handle, float* destination, uint32_t sample_count) {
    auto* self = reader(handle);
    if (self == nullptr || destination == nullptr) return;
    for (uint32_t i = 0; i < sample_count; ++i) {
        if (self->fifo_offset >= self->fifo.size() && !fillBlock(*self)) {
            destination[i] = 0.0f;
            continue;
        }
        destination[i] = static_cast<float>(self->fifo[self->fifo_offset++]) / 32767.0f;
    }
    self->consumed_samples += sample_count;
    self->position_frames = self->channels == 0
        ? 0
        : static_cast<std::uint32_t>(self->consumed_samples / self->channels);
}

// Derived from the native seek (0x180072f80): absolute mode uses the sample index
// directly; relative mode adds it to the current position. Both clamp to the total.
// The decoder seek itself is not reconstructed here; the FIFO is discarded and the
// stream is repositioned with stb_vorbis_seek_frame.
void CM3D2_CALL DLL_OGG_SeekSample(CM3D2_SoundReaderOggHandle* handle, int32_t sample, char absolute) {
    auto* self = reader(handle);
    if (self == nullptr || self->decoder == nullptr) return;
    std::int64_t target = absolute ? static_cast<std::int64_t>(sample)
                                   : static_cast<std::int64_t>(self->position_frames) + sample;
    if (target < 0) target = 0;
    if (target > static_cast<std::int64_t>(self->total_frames)) target = self->total_frames;
    stb_vorbis_seek(self->decoder, static_cast<unsigned int>(target));
    self->position_frames = static_cast<std::uint32_t>(target);
    self->consumed_samples = static_cast<std::uint64_t>(target) * self->channels;
    self->fifo.clear();
    self->fifo_offset = 0;
}

uint32_t CM3D2_CALL DLL_OGG_GetChannels(CM3D2_SoundReaderOggHandle* handle) {
    const auto* self = reader(handle);
    return self == nullptr ? 0 : self->channels;
}

uint32_t CM3D2_CALL DLL_OGG_GetSampleRate(CM3D2_SoundReaderOggHandle* handle) {
    const auto* self = reader(handle);
    return self == nullptr ? 0 : self->sample_rate;
}

uint32_t CM3D2_CALL DLL_OGG_GetSampleBit(CM3D2_SoundReaderOggHandle* handle) {
    const auto* self = reader(handle);
    return self == nullptr ? 0 : self->bits;
}

uint32_t CM3D2_CALL DLL_OGG_GetTotalSampleLengs(CM3D2_SoundReaderOggHandle* handle) {
    const auto* self = reader(handle);
    return self == nullptr ? 0 : self->total_frames;
}

} // extern "C"
