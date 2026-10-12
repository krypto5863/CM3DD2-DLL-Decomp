#include "cm3d2/file_system_api.h"
#include "file_system_wide_abi.h"
#include "support/synthetic_warc.h"
#include <cstring>
#include <cstdio>

namespace abi=cm3d2::internal::wide_abi;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::uint64_t word(const void* p,std::size_t offset) {
    std::uint64_t result;
    std::memcpy(&result,static_cast<const char*>(p)+offset,8);return result;
}
std::vector<std::wstring> names(const void* image,bool map) {
    const auto* head=reinterpret_cast<const char*>(word(image,8));
    std::vector<std::wstring> out;
    const char* node=reinterpret_cast<const char*>(word(head,0));
    while (node!=head) {
        const auto* str=node+(map?0x18:0x10);
        const auto length=word(str,0x10),capacity=word(str,0x18);
        const auto* text=capacity>7 ? reinterpret_cast<const wchar_t*>(word(str,0)) : reinterpret_cast<const wchar_t*>(str);
        out.emplace_back(text,static_cast<std::size_t>(length));
        require(out.size()<=word(image,0x10),"raw map traversal bounded by size");
        node=reinterpret_cast<const char*>(word(node,0));
    }
    require(out.size()==word(image,0x10),"raw image size");return out;
}
void* base(void* wide) {
    const auto* table=*reinterpret_cast<const std::int32_t* const*>(static_cast<char*>(wide)+8);
    return static_cast<char*>(wide)+8+table[1];
}
void exercise(CM3D2_FileSystemHandle& fs,const std::filesystem::path& first,
              const std::filesystem::path& second,bool archive) {
    void* wide=DLL_FileSystem_GetInterfaceFileSystemWidePointer(&fs);
    void* b=base(wide);
    const auto* w=abi::wideVtbl(wide);const auto* v=abi::baseVtbl(b);
    void* n=static_cast<char*>(wide)+0x10; // narrow=complete+0x20
    const auto* nt=*static_cast<const abi::NarrowVtbl* const*>(n);
    require(nt!=nullptr,"narrow callable table installed");
    const auto* nvb=*reinterpret_cast<const std::int32_t* const*>(static_cast<char*>(n)+8);
    require(static_cast<char*>(n)+8+nvb[1]==b,"narrow virtual-base adjustment");
    require(!v->is_valid(b) && !v->get_flag(b),"fresh validity/mode");
    require(v->set_flag(b,true) && v->get_flag(b),"backend mode enabled");
    require(!v->set_flag(b,false) && !v->get_flag(b),"backend mode disabled");
    void* initial=w->get_map(wide);
    require(names(initial,true).empty(),"fresh filename map");
    require((archive?DLL_FileSystem_AddArchive(&fs,first.u8string().c_str()):DLL_FileSystem_AddFolder(&fs,first.u8string().c_str()))==1,
        "first root added");
    require(v->is_valid(b) && w->exists(wide,L"one.txt") && !w->exists(wide,L"missing.txt"),"wide lookup validity");
    auto* file=w->open(wide,L"one.txt");
    require(file && file->valid() && file->size()==3,"wide open file");
    char bytes[3]{};require(file->read(bytes,3)==3 && std::memcmp(bytes,"one",3)==0,"wide file bytes");
    delete file;
    require(nt->exists(n,"ONE.TXT") && !nt->exists(n,nullptr) && !nt->exists(n,"missing.txt"),
        "narrow exists and case folding");
    auto* narrow_file=nt->open(n,"one.txt");
    require(narrow_file && narrow_file->size()==3,"narrow opens same member");
    delete narrow_file;
    require(nt->exists(n,u8"\u65e5\u672c.txt"),"narrow Japanese lookup");
    auto* japanese=nt->open(n,u8"\u65e5\u672c.txt");
    require(japanese && japanese->size()==3,"narrow Japanese file bytes size");
    delete japanese;
    alignas(std::vector<std::string>) unsigned char narrow_storage[sizeof(std::vector<std::string>)];
    auto* narrow_list=reinterpret_cast<std::vector<std::string>*>(narrow_storage);
    require(nt->list(n,narrow_list,"",3)==narrow_list && !narrow_list->empty(),"narrow constructs list");
    require(std::find(narrow_list->begin(),narrow_list->end(),u8"\u65e5\u672c.txt")!=narrow_list->end(),
        "narrow list preserves Japanese UTF-8");
    narrow_list->~vector();
    alignas(std::vector<std::wstring>) unsigned char storage[sizeof(std::vector<std::wstring>)];
    auto* list=reinterpret_cast<std::vector<std::wstring>*>(storage);
    require(w->list(wide,list,L"",3)==list && !list->empty(),"wide list construction");list->~vector();
    void* old_files=w->get_map(wide);void* old_menus=v->get_map(b);
    const auto old_names=names(old_files,true),old_menu_names=names(old_menus,false);
    require(!old_names.empty() && old_menu_names.size()==1,"populated raw containers");
    require(w->get_map(wide)==old_files && v->get_map(b)==old_menus,"unchanged generation cached");
    require((archive?DLL_FileSystem_AddArchive(&fs,second.u8string().c_str()):DLL_FileSystem_AddFolder(&fs,second.u8string().c_str()))==1,
        "second root added");
    void* new_files=w->get_map(wide);
    const auto next_names=names(new_files,true);
    require(new_files!=old_files && next_names.size()==2,"new filename generation contains newest root only");
    require(names(old_files,true)==old_names && names(old_menus,false)==old_menu_names && names(initial,true).empty(),
        "all previously published generations stay readable");
    require(names(v->get_map(b),false).size()==2,"menu set merges roots");
    if (!archive) require(word(new_files,0x38)==word(old_files,0x38) && word(new_files,0x38)>8,
        "cleared Windows map retains bucket geometry");
    require(w->exists(wide,L"two.txt"),"wide lookup sees new root");
    DLL_FileSystem_DeleteFileSystem(&fs);
}
int main() {
    try {
        using namespace synthetic_warc;
        std::filesystem::path owned;
        for (int i=0;i<100;++i) {
            auto candidate=std::filesystem::temp_directory_path()/("cm3d2-callable-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(i));
            if (std::filesystem::create_directory(candidate)) {owned=candidate;break;}
        }
        require(!owned.empty(),"fresh fixture directory");
        std::filesystem::create_directory(owned/"first");std::filesystem::create_directory(owned/"second");
        std::vector<Member> a{{"one.txt",text("one")},{"first.menu",menu("first")}};
        a.push_back({u8"\u65e5\u672c.txt",text("jp!")});
        for (int i=0;i<10;++i) a.push_back({"extra"+std::to_string(i)+".txt",text("extra")});
        const std::vector<Member> c{{"two.txt",text("two")},{"second.menu",menu("second")}};
        for (const auto& m:a) write(owned/"first"/wide(m.path),m.data);
        for (const auto& m:c) write(owned/"second"/wide(m.path),m.data);
        write(owned/"first.arc",archive(a));write(owned/"second.arc",archive(c));
        CM3D2_FileSystemHandle windows{},archives{};
        DLL_FileSystem_CreateFileSystemWindows(&windows);
        exercise(windows,owned/"first",owned/"second",false);
        DLL_FileSystem_CreateFileSystemArchive(&archives);
        exercise(archives,owned/"first.arc",owned/"second.arc",true);
        for (int kind=0;kind<2;++kind) {
            CM3D2_FileSystemHandle foreign{};
            if (kind==0) DLL_FileSystem_CreateFileSystemWindows(&foreign);
            else DLL_FileSystem_CreateFileSystemArchive(&foreign);
            void* pointer=DLL_FileSystem_GetInterfaceFileSystemWidePointer(&foreign);
            void* vbase=base(pointer);
            require(abi::baseVtbl(vbase)->deleting_destructor(vbase,1)==static_cast<char*>(pointer)-0x10,
                "direct destruction releases narrow binding before adapter storage");
            // The C handle is invalid after direct destruction; do not reuse it.
        }
        std::filesystem::remove_all(owned); // exact fresh owned fixture only
        std::puts("file_system_callable_contract: ok");return 0;
    } catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
