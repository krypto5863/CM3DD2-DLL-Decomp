// Valid generated plain/XOR/AES/zlib WARC members only. No mutation/fuzzing inputs.
// Usage: archive_decode_probe <verified original.dll.orig|rebuilt.dll>
#include "../../tests/support/synthetic_aes.h"
#include <cstdio>
#include <thread>
#include <chrono>
struct Fs { void* object; int kind; int reserved; };
static HMODULE module;
template<class T> T fn(const char* name) {
    auto p = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!p) throw std::runtime_error(name);
    return p;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(10)); ExitProcess(124); }).detach();
    try {
        module = LoadLibraryW(synthetic_warc::wide(argv[1]).c_str());
        if (!module) return 3;
        std::filesystem::path owned;
        for (int i=0;i<100;++i) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("cm3d2-decode-probe-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { owned=candidate; break; }
        }
        if (owned.empty()) return 3;
        using namespace synthetic_warc;
        std::vector<Member> members;
        for (const int size : {0,1,7,16,17,256}) {
            Bytes payload(size);
            for (int i=0;i<size;++i) payload[i]=static_cast<unsigned char>(i*37+size);
            for (int compressed=0;compressed<2;++compressed) for (int xor_prefix=0;xor_prefix<2;++xor_prefix)
                members.push_back({"n"+std::to_string(size)+"c"+std::to_string(compressed)+"x"+std::to_string(xor_prefix)+".bin",payload,compressed!=0,xor_prefix!=0});
        }
        const auto file = owned/"valid.arc";
        const auto encrypted=aesMembers();
        members.insert(members.end(),encrypted.begin(),encrypted.end());
        write(file,archive(members));
        Fs fs{}; fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_CreateFileSystemArchive")(&fs);
        std::printf("mount=%u\n",fn<unsigned char(__cdecl*)(Fs*,const char*)>("DLL_FileSystem_AddArchive")(&fs,file.u8string().c_str()));
        // Probe OS sharing without changing the file's contents.
        HANDLE writer = CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        std::printf("write-open-denied=%u\n",writer==INVALID_HANDLE_VALUE);
        if (writer!=INVALID_HANDLE_VALUE) CloseHandle(writer);
        for (int pass=0;pass<2;++pass) for (const auto& member : members) {
            void* data=nullptr;
            const auto got=fn<unsigned char(__cdecl*)(Fs*,const char*,void**)>("DLL_FileSystem_GetFile")(&fs,member.path.c_str(),&data);
            std::uint64_t size=0, hash=0xcbf29ce484222325ULL;
            if (got) {
                size=fn<std::uint64_t(__cdecl*)(void**)>("DLL_File_GetSize")(&data);
                unsigned char bytes[512]{};
                const auto n=fn<std::uint64_t(__cdecl*)(void**,void*,std::uint64_t)>("DLL_File_Read")(&data,bytes,sizeof(bytes));
                if (n>sizeof(bytes)) throw std::runtime_error("read bound");
                if (size!=member.data.size() || n!=member.data.size() ||
                    (n && std::memcmp(bytes,member.data.data(),static_cast<std::size_t>(n))!=0))
                    throw std::runtime_error("decoded fixture mismatch");
                for (std::uint64_t i=0;i<n;++i) hash=(hash^bytes[i])*0x100000001b3ULL;
                fn<void(__cdecl*)(void**)>("DLL_File_CloseFile")(&data);
            }
            if (!got) throw std::runtime_error("valid fixture open failed");
            std::printf("%d %s got=%u size=%llu hash=%016llx\n",pass,member.path.c_str(),got,
                static_cast<unsigned long long>(size),static_cast<unsigned long long>(hash));
        }
        fn<void(__cdecl*)(Fs*)>("DLL_FileSystem_DeleteFileSystem")(&fs);
        std::filesystem::remove_all(owned);
        FreeLibrary(module);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
