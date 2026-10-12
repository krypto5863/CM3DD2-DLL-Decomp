#include "warc_archive.h"
#include "support/synthetic_aes.h"
#include <cstring>
#include <thread>
#include <atomic>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    std::filesystem::path owned;
    try {
        for (int i=0;i<100;++i) {
            const auto p=std::filesystem::temp_directory_path()/
                ("cm3d2-owned-stream-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(i));
            if (std::filesystem::create_directory(p)) {owned=p;break;}
        }
        require(!owned.empty(), "owned fixture directory");
        using namespace synthetic_warc;
        Bytes payload(256);
        for (std::size_t i=0;i<payload.size();++i) payload[i]=static_cast<unsigned char>(i);
        const auto path=owned/"valid.arc";
        auto members=aesMembers();
        members.insert(members.end(),{{"empty.bin",{}},{"plain.bin",payload},{"xor.bin",payload,false,true},
                            {"zlib.bin",payload,true,false},{"both.bin",payload,true,true}});
        write(path,archive(members));
        {
            cm3d2::internal::WarcArchive fs;
            require(cm3d2::internal::WarcArchive::open(path.wstring(),fs), "open retained mount");
            auto copy=fs; // copies share the mounted stream rather than reopening the path
            fs=cm3d2::internal::WarcArchive{};
            HANDLE writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            if (writer!=INVALID_HANDLE_VALUE) CloseHandle(writer);
            require(writer==INVALID_HANDLE_VALUE, "retained mount denies concurrent writer");
            unsigned char head[8]{};
            require(copy.readHead(head,sizeof(head)) && std::memcmp(head,"warc",4)==0, "mounted head read");
            for (const auto& member : members) {
                std::uint64_t value=0; require(copy.find(wide(member.path),value,true),"find valid fixture");
                std::unique_ptr<std::byte[]> data; std::uint32_t length=0;
                require(copy.readOwned(value,data,length) && data && length==member.data.size() &&
                    (!length || std::memcmp(data.get(),member.data.data(),length)==0),"valid fixture decoded bytes");
            }
            for (const wchar_t* name : {L"plain.bin",L"xor.bin",L"zlib.bin",L"both.bin"}) {
                std::uint64_t value=0; require(copy.find(name,value,true), "find member");
                std::unique_ptr<std::byte[]> buffer;
                std::uint32_t size=0;
                require(copy.readOwned(value,buffer,size) && size==payload.size() &&
                        std::memcmp(buffer.get(),payload.data(),size)==0, "owned decode bytes");
            }
            std::uint64_t empty=0; require(copy.find(L"empty.bin",empty,true), "find empty");
            std::unique_ptr<std::byte[]> buffer;
            std::uint32_t size=99;
            require(copy.readOwned(empty,buffer,size) && buffer && size==0, "empty owned allocation remains valid");
            std::uint64_t compressed=0; require(copy.find(L"both.bin",compressed,true), "concurrent member");
            std::atomic<bool> good{true};
            auto read_many=[&] {
                for (int i=0;i<20;++i) {
                    std::unique_ptr<std::byte[]> data; std::uint32_t length=0;
                    if (!copy.readOwned(compressed,data,length) || length!=payload.size() ||
                        std::memcmp(data.get(),payload.data(),length)!=0) good=false;
                }
            };
            std::thread a(read_many), b(read_many); a.join(); b.join();
            require(good, "shared mount concurrent reads serialize correctly");
        }
        HANDLE writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        require(writer!=INVALID_HANDLE_VALUE, "last owner releases mount handle"); CloseHandle(writer);
        std::filesystem::remove_all(owned); // exact fresh fixture directory only
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
