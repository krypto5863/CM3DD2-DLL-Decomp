// Probes the ORIGINAL Windows file system on a scratch directory tree (read-only use).
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
struct FsHandle { void* object; int32_t kind; int32_t reserved; };
typedef void(__cdecl* FnCreate)(FsHandle*);
typedef uint8_t(__cdecl* FnStr)(FsHandle*, const char*);
typedef uint8_t(__cdecl* FnVoid)(FsHandle*);
typedef uint8_t(__cdecl* FnGetFile)(FsHandle*, const char*, void**);
typedef uint64_t(__cdecl* FnSize)(void**);
typedef uint64_t(__cdecl* FnRead)(void**, void*, uint64_t);
typedef void(__cdecl* FnClose)(void**);
static HMODULE m;
template<class T> T fn(const char* n){ return reinterpret_cast<T>(GetProcAddress(m,n)); }
int main(int argc,char**argv){
  setvbuf(stdout,nullptr,_IONBF,0);
  m=LoadLibraryA(argv[1]);
  auto create=fn<FnCreate>("DLL_FileSystem_CreateFileSystemWindows");
  auto setbase=fn<FnStr>("DLL_FileSystem_SetBaseDirectory");
  auto addf=fn<FnStr>("DLL_FileSystem_AddFolder");
  auto adda=fn<FnStr>("DLL_FileSystem_AddArchive");
  auto addap=fn<FnStr>("DLL_FileSystem_AddAutoPath");
  auto valid=fn<FnVoid>("DLL_FileSystem_IsValid");
  auto exists=fn<FnStr>("DLL_FileSystem_IsExistentFile");
  auto get=fn<FnGetFile>("DLL_FileSystem_GetFile");
  auto size=fn<FnSize>("DLL_File_GetSize");
  auto rd=fn<FnRead>("DLL_File_Read");
  auto cl=fn<FnClose>("DLL_File_CloseFile");
  std::string root=argv[2]; // scratch root with trailing no slash
  FsHandle fs{}; create(&fs);
  printf("valid(fresh)=%d\n",valid(&fs));
  printf("setbase(no colon)=%d\n",setbase(&fs,"relative"));
  printf("addfolder(missing)=%d\n",addf(&fs,(root+"\nope").c_str()));
  printf("valid(after failed add)=%d\n",valid(&fs));
  printf("addfolder(root)=%d\n",addf(&fs,root.c_str()));
  printf("addfolder(root again)=%d\n",addf(&fs,root.c_str()));
  std::string up=root; for(auto&c:up) c=toupper(c);
  printf("addfolder(root upper)=%d\n",addf(&fs,up.c_str()));
  printf("addfolder(root slash)=%d\n",addf(&fs,(root+"/").c_str()));
  printf("valid(after add)=%d\n",valid(&fs));
  std::vector<std::string> names; std::ifstream in(argv[3]); for(std::string l;std::getline(in,l);){ if(!l.empty()&&l.back()=='\r')l.pop_back(); if(!l.empty())names.push_back(l);}
  for(auto&n:names){ void*f=nullptr; uint8_t ok=get(&fs,n.c_str(),&f); std::string c;
    if(ok&&f){ uint64_t s=size(&f); std::vector<char> b(s+1,0); rd(&f,b.data(),s); c.assign(b.data(),s); cl(&f);} 
    printf("name[%s] exists=%d get=%d content=[%s]\n",n.c_str(),exists(&fs,n.c_str()),ok,c.c_str()); }
  printf("addarchive(Windows,'zzz')=%d addautopath('zzz')=%d\n",adda(&fs,"zzz"),addap(&fs,"zzz"));
  return 0;
}
