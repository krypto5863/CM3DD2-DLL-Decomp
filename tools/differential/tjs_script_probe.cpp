// Load each DLL in a separate process. Only synthetic, side-effect-free scripts
// are evaluated. Usage: tjs_script_probe.exe <dll> [errors]
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
HMODULE module;
void* active_script;
std::uint64_t active_id;
template<class T> T fn(const char* name) {
    auto address = GetProcAddress(module, name);
    if (!address) { std::fprintf(stderr, "Missing export: %s\n", name); std::exit(3); }
    return reinterpret_cast<T>(address);
}
using VPP = void**;
using Run = void(__cdecl*)(VPP, const char*, void*);
using RunFile = void(__cdecl*)(VPP, void*, void*);
using Callback = void(__cdecl*)(const void*, void*);
using ConsoleCallback = void(__cdecl*)(const char*, std::uint8_t);
void hex(const char* text) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        std::printf("%02x", static_cast<unsigned>(*p));
}
void observe(const char* label, void* raw) {
    void* v = nullptr;
    fn<void(__cdecl*)(VPP,void*)>("DLL_TJSVariant_Reference")(&v,raw);
    const int type = fn<int(__cdecl*)(VPP)>("DLL_TJSVariant_GetType")(&v);
    char text[512] = {};
    fn<void(__cdecl*)(VPP,char*,int)>("DLL_TJSVariant_AsString")(&v,text,sizeof(text));
    std::printf("%s type=%d",label,type);
    if (type == 4) std::printf(" int=%lld",static_cast<long long>(fn<std::int64_t(__cdecl*)(VPP)>("DLL_TJSVariant_AsInteger")(&v)));
    if (type == 5) std::printf(" real=%.9g",static_cast<double>(fn<float(__cdecl*)(VPP)>("DLL_TJSVariant_AsReal")(&v)));
    std::printf(" utf8="); hex(text); std::printf("\n");
}
void setInteger(void* raw, int value) {
    if (!raw) { std::printf("callback.result.null\n"); return; }
    void* v = nullptr;
    fn<void(__cdecl*)(VPP,void*)>("DLL_TJSVariant_Reference")(&v,raw);
    fn<void(__cdecl*)(VPP,int)>("DLL_TJSVariant_SetInteger")(&v,value);
}
void __cdecl host(const void* parameters, void* result) {
    const int count = fn<int(__cdecl*)(const void*)>("DLL_TJS_ParamAccess_Size")(parameters);
    const auto id = fn<std::uint64_t(__cdecl*)(const void*)>("DLL_TJS_ParamAccess_GetClassUniqueId")(parameters);
    const auto key = fn<std::uint64_t(__cdecl*)(const void*)>("DLL_TJS_ParamAccess_GetNameKey")(parameters);
    std::printf("host argc=%d id_matches=%d name_key=%016llx\n",count,id == active_id,static_cast<unsigned long long>(key));
    for (int i=0;i<count;++i) {
        char label[40]; std::snprintf(label,sizeof(label),"host.arg%d",i);
        observe(label,fn<void*(__cdecl*)(const void*,int)>("DLL_TJS_ParamAccess_At")(parameters,i));
    }
    setInteger(result,37);
}
void __cdecl replacement(const void*, void* result) { std::printf("replacement.called\n"); setInteger(result,91); }
void __cdecl console(const char* message, std::uint8_t exception) {
    // TJS error VM dumps include ASLR/heap addresses; normalize only address-like
    // 0x tokens (8+ hex digits), retaining the text and callback ordering.
    std::string normalized;
    for (const char* p=message; *p;) {
        if (p[0]=='0' && p[1]=='x') {
            const char* end=p+2;
            while ((*end>='0' && *end<='9') || (*end>='a' && *end<='f') || (*end>='A' && *end<='F')) ++end;
            if (end-(p+2)>=8) { normalized += "0xADDR"; p=end; continue; }
        }
        normalized += *p++;
    }
    std::printf("console exception=%u utf8=",static_cast<unsigned>(exception)); hex(normalized.c_str()); std::printf("\n");
}
void run(const char* label, const char* code, void* result, bool evaluate = true) {
    fn<Run>(evaluate ? "DLL_TJS_EvalScript" : "DLL_TJS_ExecScript")(&active_script,code,result);
    observe(label,result);
}
void file(const char* label, const std::vector<unsigned char>& bytes, void* result, bool evaluate) {
    void* memory = nullptr;
    fn<void(__cdecl*)(VPP)>("DLL_FileMemory_CreateileMemory")(&memory);
    fn<void(__cdecl*)(VPP,const void*,int)>("DLL_FileMemory_SetData")(&memory,bytes.data(),static_cast<int>(bytes.size()));
    void* interface_file = fn<void*(__cdecl*)(VPP)>("DLL_FileMemory_GetInterfaceFilePointer")(&memory);
    fn<RunFile>(evaluate ? "DLL_TJS_EvalScriptFile" : "DLL_TJS_ExecScriptFile")(&active_script,interface_file,result);
    observe(label,result);
    std::printf("%s tell=%llu\n",label,static_cast<unsigned long long>(fn<std::uint64_t(__cdecl*)(VPP)>("DLL_File_GetTell")(&memory)));
    fn<void(__cdecl*)(VPP)>("DLL_FileMemory_DeleteileMemory")(&memory);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if(argc < 2) return 2;
    module = LoadLibraryA(argv[1]);
    if(!module) { std::fprintf(stderr,"LoadLibrary failed: %lu\n",GetLastError()); return 3; }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const char* required[] = {"DLL_TJS_CreateTjsScriptClass","DLL_TJS_CreateTjsConsoleClass","DLL_TJS_DeleteTjsScriptClass","DLL_TJS_ExecScript","DLL_TJS_ExecScriptFile","DLL_TJS_EvalScript","DLL_TJS_EvalScriptFile","DLL_TJS_AddMemberFunction","DLL_TJS_GetUniqueID","DLL_TJS_ParamAccess_Size","DLL_TJS_ParamAccess_GetClassUniqueId","DLL_TJS_ParamAccess_GetNameKey","DLL_TJS_ParamAccess_At"};
    for(auto name:required) fn<void*>(name);
    void* result = nullptr;
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Create")(&result);
    fn<void(__cdecl*)(void*,VPP)>("DLL_TJS_CreateTjsScriptClass")(nullptr,&active_script);
    active_id=fn<std::uint64_t(__cdecl*)(VPP)>("DLL_TJS_GetUniqueID")(&active_script);
    std::printf("created=%d id=%llu\n",active_script!=nullptr,static_cast<unsigned long long>(active_id));
    fn<void(__cdecl*)(VPP,ConsoleCallback)>("DLL_TJS_CreateTjsConsoleClass")(&active_script,console);
    try {
        if(argc > 2 && std::strcmp(argv[2],"errors")==0) {
            setInteger(result,123);
            fn<RunFile>("DLL_TJS_EvalScriptFile")(&active_script,nullptr,result);
            observe("null.file",result);
            file("empty.file",{},result,true);
            file("one.byte",{'1'},result,true);
            file("utf32le",{0xff,0xfe,0,0,'1',0,0,0},result,true);
            file("utf16be",{0xfe,0xff,0,'1'},result,true);
            run("syntax.error","1 +",result);
        } else {
            run("integer","1+2*3",result);
            run("large.integer","4294967296+123",result);
            run("real","1.25+2.5",result);
            run("string","\"hello\" + \" world\"",result);
            run("unicode","\"\xE6\x97\xA5\xE6\x9C\xAC\"",result);
            run("void","void",result);
            run("empty","",result);
            run("exec","var shared = 12; shared += 5;",result,false);
            run("persistent","shared*2",result);
            run("function.exec","function twice(x) { return x*2; }",result,false);
            run("function.eval","twice(19)",result);
            run("array","[3,5,8].join(\",\")",result);
            run("dictionary","%[\"a\"=>11,\"b\"=>7].a",result);
            run("math","Math.sqrt(81)",result);
            run("string.length","\"abcdef\".length",result);
            auto add=fn<void(__cdecl*)(VPP,const char*,Callback)>("DLL_TJS_AddMemberFunction");
            add(&active_script,"host",host);
            run("host.result","host(12,\"value\",2.5,4294967296)",result);
            run("host.noargs","host()",result);
            run("host.statement","host();",result,false);
            add(&active_script,"host",replacement);
            run("host.replaced","host(1)",result);
            add(&active_script,"host",nullptr);
            run("host.removed","host()",result);
            file("cp932",{'\"',0x93,0xfa,0x96,0x7b,'\"'},result,true);
            file("utf8.bom",{0xef,0xbb,0xbf,'\"',0xe6,0x97,0xa5,0xe6,0x9c,0xac,'\"'},result,true);
            file("utf16le",{0xff,0xfe,'\"',0,0xe5,0x65,0x2c,0x67,'\"',0},result,true);
            const std::string code="var fileGlobal = 73;";
            file("exec.file",std::vector<unsigned char>(code.begin(),code.end()),result,false);
            run("exec.file.global","fileGlobal",result);
        }
    } catch(...) { std::printf("exception.escaped\n"); }
    fn<void(__cdecl*)(VPP)>("DLL_TJS_DeleteTjsScriptClass")(&active_script);
    std::printf("deleted=%d\n",active_script==nullptr);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Delete")(&result);
    FreeLibrary(module);
    return 0;
}
