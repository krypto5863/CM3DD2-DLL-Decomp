// Oracle v2: drives the ORIGINAL cm3d2.dll OGG exports. The reader's file system is a
// FileSystemDelegate whose GetFile callback returns a memory file holding the Ogg bytes.
// Usage: ogg_oracle2.exe <dll> <ogg path> <out raw floats>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

typedef void(__cdecl* FnVoidVPP)(void**);
typedef void(__cdecl* FnCreate)(void*, void**);
typedef void(__cdecl* FnOpen)(void**, const char*);
typedef uint8_t(__cdecl* FnIsOpen)(void**);
typedef uint16_t(__cdecl* FnU16)(void**);
typedef uint32_t(__cdecl* FnU32)(void**);
typedef void(__cdecl* FnReadUnity)(void**, float*, uint32_t);
typedef void(__cdecl* FnSeek)(void**, int32_t, char);
typedef void(__cdecl* FnCloseOrDelete)(void**);
typedef void(__cdecl* FnSetCb)(void**, void* (__cdecl*)(const wchar_t*));
typedef void(__cdecl* FnMemCreate)(void**);
typedef void(__cdecl* FnMemSet)(void**, const void*, int32_t);
typedef void*(__cdecl* FnMemIface)(void**);

static HMODULE g_module = nullptr;
static std::vector<unsigned char> g_bytes;
static void* g_memory = nullptr;
static void* g_memory_iface = nullptr;

template <typename T>
T fn(const char* name) {
    FARPROC p = GetProcAddress(g_module, name);
    if (p == nullptr) { std::printf("MISSING %s\n", name); std::exit(3); }
    return reinterpret_cast<T>(p);
}

// Returns the interface pointer of the memory file that holds the Ogg bytes.
void* provideFile(const wchar_t* path) {
    std::printf("callback path=%ls\n", path);
    return g_memory_iface;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 4) return 2;
    g_module = LoadLibraryA(argv[1]);
    if (!g_module) { std::printf("load failed\n"); return 1; }

    // Load the file bytes.
    FILE* in = nullptr;
    fopen_s(&in, argv[2], "rb");
    if (!in) { std::printf("cannot read ogg\n"); return 1; }
    std::fseek(in, 0, SEEK_END);
    const long size = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);
    g_bytes.resize(static_cast<size_t>(size));
    std::fread(g_bytes.data(), 1, g_bytes.size(), in);
    std::fclose(in);

    // Memory file holding the bytes.
    auto mem_create = fn<FnVoidVPP>("DLL_FileMemory_CreateileMemory");
    auto mem_set = fn<FnMemSet>("DLL_FileMemory_SetData");
    auto mem_iface = fn<FnMemIface>("DLL_FileMemory_GetInterfaceFilePointer");
    mem_create(&g_memory);
    mem_set(&g_memory, g_bytes.data(), static_cast<int32_t>(g_bytes.size()));
    g_memory_iface = mem_iface(&g_memory);
    std::printf("memory file iface=%p size=%zu\n", g_memory_iface, g_bytes.size());

    // Delegate file system that serves that file for any name.
    auto delegate_create = fn<FnVoidVPP>("DLL_FileSystemDelegate_Create");
    auto delegate_register = fn<FnSetCb>("DLL_FileSystemDelegate_RegisterGetFileWideCallBack");
    void* delegate = nullptr;
    delegate_create(&delegate);
    delegate_register(&delegate, provideFile);

    auto create = fn<FnCreate>("DLL_OGG_CreateSoundReaderOGG");
    auto open = fn<FnOpen>("DLL_OGG_OpenFile");
    auto is_open = fn<FnIsOpen>("DLL_OGG_IsOpenFile");
    auto channels = fn<FnU16>("DLL_OGG_GetChannels");
    auto bits = fn<FnU16>("DLL_OGG_GetSampleBit");
    auto rate = fn<FnU32>("DLL_OGG_GetSampleRate");
    auto total = fn<FnU32>("DLL_OGG_GetTotalSampleLengs");
    auto read_unity = fn<FnReadUnity>("DLL_OGG_ReadUnity");
    auto close = fn<FnCloseOrDelete>("DLL_OGG_CloseFile");
    auto del = fn<FnCloseOrDelete>("DLL_OGG_DeleteSoundReaderOGG");

    void* reader = nullptr;
    create(delegate, &reader);
    open(&reader, "stream.ogg");
    std::printf("isopen=%d\n", is_open(&reader));
    if (!is_open(&reader)) return 0;
    const uint32_t frames = total(&reader);
    std::printf("channels=%u bits=%u rate=%u total=%u\n", channels(&reader), bits(&reader), rate(&reader), frames);

    FILE* out = nullptr;
    fopen_s(&out, argv[3], "wb");
    const uint32_t chunk = 4096;
    std::vector<float> buffer(chunk);
    const uint64_t target = static_cast<uint64_t>(frames) * channels(&reader);
    uint64_t produced = 0;
    while (produced < target) {
        const uint32_t take = static_cast<uint32_t>((target - produced) < chunk ? (target - produced) : chunk);
        read_unity(&reader, buffer.data(), take);
        std::fwrite(buffer.data(), sizeof(float), take, out);
        produced += take;
    }
    std::fclose(out);
    std::printf("produced=%llu\n", static_cast<unsigned long long>(produced));
    close(&reader);
    del(&reader);
    return 0;
}
