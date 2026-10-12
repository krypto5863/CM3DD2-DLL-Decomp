#include "file_system_archive.h"
#include "file_system_windows.h"
#include "support/synthetic_warc.h"
#include <cstdio>

using namespace synthetic_warc;
using cm3d2::internal::ArchiveFolderSet;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
std::uint64_t wideKey(const std::wstring& name) {
    std::uint64_t h=0xcbf29ce484222325ULL;
    for (auto unit:name) {
        h=(h^(unit&255))*0x100000001b3ULL;
        h=(h^((unit>>8)&255))*0x100000001b3ULL;
    }
    return h^(h>>32);
}
Bytes rootTable(std::uint64_t dir,std::uint64_t file,std::uint64_t nested_offset) {
    Bytes root,child;
    u32(root,24);u32(root,16);u64(root,0);u32(root,1);u32(root,1);
    u64(root,dir);u64(root,56);u64(root,file);u64(root,0);
    u32(child,24);u32(child,16);u64(child,dir);u32(child,0);u32(child,1);
    u64(child,file);u64(child,nested_offset);
    root.insert(root.end(),child.begin(),child.end());return root;
}
Bytes fixture(const std::wstring& directory,const std::string& encoded) {
    Bytes data=plain(text("root"));
    const auto nested_offset=data.size();
    const auto nested=plain(text("nested"));data.insert(data.end(),nested.begin(),nested.end());
    const Bytes sections[]={rootTable(wideKey(directory),wideKey(L"pick.txt"),nested_offset),
        rootTable(key(encoded,true),key("pick.txt",true),nested_offset),data,plain({})};
    Bytes out{0x77,0x61,0x72,0x63,0xff,0xaa,0x45,0xf1};u32(out,1000);u32(out,4);
    for (std::uint32_t tag=0;tag<4;++tag) {
        u32(out,tag);u64(out,sections[tag].size());out.insert(out.end(),sections[tag].begin(),sections[tag].end());
    }
    return out;
}
std::string read(const ArchiveFolderSet& fs,bool wide) {
    ArchiveFolderSet::Hit hit;
    const std::vector<std::uint64_t> keys{wide ? wideKey(L"pick.txt") : key("pick.txt",true)};
    require(wide ? fs.findWideHashes(keys,true,hit) : fs.findHashes(keys,true,hit),"find expected table");
    Bytes bytes;require(hit.archive->read(hit.value,bytes),"decode plain member");return {bytes.begin(),bytes.end()};
}
int main() {
    try {
        std::filesystem::path owned;
        for (int i=0;i<100;++i) {
            auto p=std::filesystem::temp_directory_path()/
                ("cm3d2-autopath-facet-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(i));
            if (std::filesystem::create_directory(p)) {owned=p;break;}
        }
        require(!owned.empty(),"fresh fixture directory");
        struct Case {std::wstring wide;std::string bytes;};
        const std::vector<Case> cases{
            {L"ascii","ascii"},{L"caf\u00e9","caf\xc3\xa9"},
            {L"\u65e5\u672c","\xe6\x97\xa5\xe6\x9c\xac"},
            {std::wstring(1,0xd83d)+static_cast<wchar_t>(0xde00),"\xed\xa0\xbd\xed\xb8\x80"},
            {std::wstring(1,0xd800),"\xed\xa0\x80"},{std::wstring(1,0xf600),"\xef\x98\x80"}};
        for (std::size_t i=0;i<cases.size();++i) {
            const auto& c=cases[i];
            require(cm3d2::internal::archiveNameKeyUtf8(c.wide)==key(c.bytes,true),"native per-unit tag1 encoding");
            const auto path=owned/(std::to_string(i)+".arc");write(path,fixture(c.wide,c.bytes));
            ArchiveFolderSet fs;fs.setTreeMode(true);
            require(fs.addArchive(cm3d2::internal::normalizeFileSystemName(path.wstring())),"mount valid generated archive");
            require(read(fs,false)=="root" && read(fs,true)=="root","root precedence before auto path");
            require(fs.addAutoPath(c.wide),"register wide auto path");
            require(read(fs,false)=="nested" && read(fs,true)=="nested","auto path attaches both encodings");
            require(!fs.addAutoPath(c.wide),"duplicate auto path rejected");
        }
        const std::wstring terminated=L"abc"+std::wstring(1,L'\0')+L"ignored";
        require(cm3d2::internal::archiveNameKeyUtf8(terminated)==key("abc",true),"terminated converter range");
        std::filesystem::remove_all(owned); // exact fresh owned directory only
        std::puts("archive_autopath_conversion_contract: ok");return 0;
    } catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
