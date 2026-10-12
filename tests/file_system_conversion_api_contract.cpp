#include "cm3d2/file_system_api.h"
#include "file_system_wide_abi.h"
#include "support/synthetic_warc.h"
#include <cstring>
#include <cstdio>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::string read(CM3D2_FileSystemHandle& fs,const char* name) {
    CM3D2_FileHandle file=nullptr;
    require(DLL_FileSystem_GetFile(&fs,name,&file)==1,"C export opens converted filename");
    std::string result(static_cast<std::size_t>(DLL_File_GetSize(&file)),0);
    require(DLL_File_Read(&file,result.data(),result.size())==result.size(),"C export reads file");
    DLL_File_CloseFile(&file);return result;
}
std::vector<std::string> list(CM3D2_FileSystemHandle& fs,bool name_map) {
    CM3D2_FileSystemList holder{};
    if (name_map) DLL_FileSystem_GetNameMapLastArchive(&fs,L"",&holder);
    else DLL_FileSystem_CreateList(&fs,"",3,&holder);
    std::vector<std::string> result;
    for (int i=0;i<holder.count;++i) {
        char bytes[256]{};
        const auto count=DLL_FileSystem_AtList(&holder,i,bytes,sizeof bytes);
        require(count>0 && count==std::strlen(bytes),"C list item byte count");
        result.emplace_back(bytes);
    }
    DLL_FileSystem_DeleteList(&holder);return result;
}
}
int main() {
    try {
        using namespace synthetic_warc;
        std::filesystem::path owned;
        for (int i=0;i<100;++i) {
            auto candidate=std::filesystem::temp_directory_path()/
                ("cm3d2-conversion-api-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(i));
            if (std::filesystem::create_directory(candidate)) {owned=candidate;break;}
        }
        require(!owned.empty(),"fresh fixture directory");
        const std::wstring low_name=std::wstring(1,static_cast<wchar_t>(0xf600))+L".txt";
        const std::wstring paired_name=std::wstring(1,static_cast<wchar_t>(0xd83d))+static_cast<wchar_t>(0xde00)+L".txt";
        write(owned/low_name,text("native-low16"));
        write(owned/paired_name,text("os-surrogate"));
        write(owned/L"\u65e5\u672c.txt",text("Japanese"));
        CM3D2_FileSystemHandle fs{};
        DLL_FileSystem_CreateFileSystemWindows(&fs);
        require(DLL_FileSystem_AddFolder(&fs,owned.u8string().c_str())==1,"add generated root");
        const char* supplementary="\xf0\x9f\x98\x80.txt";
        require(DLL_FileSystem_IsExistentFile(&fs,supplementary)==1,"C exists accepts supplementary input");
        require(read(fs,supplementary)=="native-low16","C open uses low16, not Windows surrogate-pair decoding");
        require(read(fs,"\xe6\x97\xa5\xe6\x9c\xac.txt")=="Japanese","Japanese C lookup preserved");
        namespace abi=cm3d2::internal::wide_abi;
        void* w=DLL_FileSystem_GetInterfaceFileSystemWidePointer(&fs);
        void* n=static_cast<char*>(w)+0x10;
        const auto* nt=*static_cast<const abi::NarrowVtbl* const*>(n);
        auto* direct=nt->open(n,supplementary);
        require(direct && direct->size()==std::strlen("native-low16"),"narrow slot agrees with C export");
        delete direct;
        auto* actual_pair=abi::wideVtbl(w)->open(w,paired_name.c_str());
        require(actual_pair && actual_pair->size()==std::strlen("os-surrogate"),"wide API preserves actual UTF-16 pair");
        delete actual_pair;
        for (bool name_map : {false,true}) {
            const auto output=list(fs,name_map);
            require(std::find(output.begin(),output.end(),"\xef\x98\x80.txt")!=output.end(),"C output encodes low16 unit");
            require(std::find(output.begin(),output.end(),"\xed\xa0\xbd\xed\xb8\x80.txt")!=output.end(),
                "C output encodes surrogate units separately like native facet");
        }
        // Local reconstructed-code checks only; no malformed-original execution.
        // /EHsc assumes declared extern-C calls do not throw; use a dynamically
        // resolved C++ function-pointer type so this local diagnostic has an EH edge.
        using ExistsCall=std::uint32_t(__cdecl*)(CM3D2_FileSystemHandle*,const char*);
        auto throwing_exists=reinterpret_cast<ExistsCall>(GetProcAddress(
            GetModuleHandleW(L"cm3d2_reconstructed.dll"),"DLL_FileSystem_IsExistentFile"));
        require(throwing_exists!=nullptr,"resolve exception diagnostic entry point");
        bool threw=false;
        try { throwing_exists(&fs,"\xc3"); }
        catch (const std::range_error& error) { threw=std::string(error.what())=="bad conversion"; }
        require(threw,"C export preserves conversion error instead of Windows replacement");
        DLL_FileSystem_DeleteFileSystem(&fs);
        std::filesystem::remove_all(owned); // exact fresh owned fixture directory only
        std::puts("file_system_conversion_api_contract: ok");return 0;
    } catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
