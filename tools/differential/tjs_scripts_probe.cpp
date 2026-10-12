// Standalone differential probe for native Scripts. Only synthetic sources and
// delegate-served virtual files are executed. Storage takes ownership of files.
// Usage: tjs_scripts_probe.exe <dll>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
HMODULE module;
void* script;
unsigned exceptions;
template<class T> T fn(const char* name) {
    auto p = GetProcAddress(module, name);
    if (!p) { std::fprintf(stderr, "Missing export: %s\n", name); std::exit(3); }
    return reinterpret_cast<T>(p);
}
using VPP = void**;
using Run = void(__cdecl*)(VPP, const char*, void*);
void hex(const char* text) {
    // Object conversion messages/paths embed unstable heap pointers. Normalize
    // only 0x tokens with at least eight hex digits, retaining all other text.
    std::string normalized;
    for (auto p=text; *p;) {
        if (p[0]=='0' && p[1]=='x') {
            auto end=p+2;
            while ((*end>='0' && *end<='9') || (*end>='a' && *end<='f') || (*end>='A' && *end<='F')) ++end;
            if (end-(p+2)>=8) { normalized += "0xADDR"; p=end; continue; }
        }
        normalized += *p++;
    }
    for (auto p = reinterpret_cast<const unsigned char*>(normalized.c_str()); *p; ++p)
        std::printf("%02x", static_cast<unsigned>(*p));
}
void __cdecl console(const char*, std::uint8_t error) { exceptions += error != 0; }
void run(const char* label, const char* code, void* result, bool eval = true) {
    exceptions = 0;
    fn<void(__cdecl*)(VPP,int)>("DLL_TJSVariant_SetInteger")(&result,123);
    fn<Run>(eval ? "DLL_TJS_EvalScript" : "DLL_TJS_ExecScript")(&script,code,result);
    char text[1024] = {};
    const int type = fn<int(__cdecl*)(VPP)>("DLL_TJSVariant_GetType")(&result);
    // Never stringify host object/octets: wrapper payloads are unresolved and
    // native object text contains process-specific addresses.
    if (type != 1 && type != 3)
        fn<void(__cdecl*)(VPP,char*,int)>("DLL_TJSVariant_AsString")(&result,text,sizeof(text));
    std::printf("%s type=%d exceptions=%u utf8=",label,type,exceptions);
    hex(text); std::printf("\n");
}
void caught(const char* label, const char* expression, void* result) {
    exceptions = 0;
    const std::string code = std::string("var caught='none'; try { ")+expression+"; } catch(e) { caught=e.message; }";
    fn<Run>("DLL_TJS_ExecScript")(&script,code.c_str(),nullptr);
    std::printf("%s trapped.console_exceptions=%u\n",label,exceptions);
    run(label,"caught",result);
}
void* __cdecl provide(const wchar_t* path) {
    char utf8[1024] = {};
    WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),nullptr,nullptr);
    std::printf("storage.request utf8="); hex(utf8); std::printf("\n");
    std::vector<unsigned char> bytes;
    if (std::wcscmp(path,L"exec.tjs") == 0) {
        const std::string source = "var storageValue = 61;";
        bytes.assign(source.begin(),source.end());
    } else if (std::wcscmp(path,L"eval.tjs") == 0) {
        const std::string source = "20+22";
        bytes.assign(source.begin(),source.end());
    } else if (std::wcscmp(path,L"cp932.tjs") == 0) {
        bytes = {'\"',0x93,0xfa,0x96,0x7b,'\"'};
    } else if (std::wcscmp(path,L"utf8.tjs") == 0) {
        bytes = {0xef,0xbb,0xbf,'\"',0xe6,0x97,0xa5,0xe6,0x9c,0xac,'\"'};
    } else if (std::wcscmp(path,L"utf16.tjs") == 0) {
        bytes = {0xff,0xfe,'\"',0,0xe5,0x65,0x2c,0x67,'\"',0};
    } else if (std::wcscmp(path,L"empty.tjs") != 0) {
        std::printf("storage.missing\n"); return nullptr;
    }
    void* memory = nullptr;
    fn<void(__cdecl*)(VPP)>("DLL_FileMemory_CreateileMemory")(&memory);
    fn<void(__cdecl*)(VPP,const void*,int)>("DLL_FileMemory_SetData")(&memory,bytes.data(),static_cast<int>(bytes.size()));
    std::printf("storage.provided bytes=%zu\n",bytes.size());
    // Scripts' storage method deletes this via the IFile deleting destructor.
    return fn<void*(__cdecl*)(VPP)>("DLL_FileMemory_GetInterfaceFilePointer")(&memory);
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 2) return 2;
    module = LoadLibraryA(argv[1]);
    if (!module) { std::fprintf(stderr,"LoadLibrary failed: %lu\n",GetLastError()); return 3; }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    void* delegate = nullptr;
    fn<void(__cdecl*)(VPP)>("DLL_FileSystemDelegate_Create")(&delegate);
    fn<void(__cdecl*)(VPP,void*(__cdecl*)(const wchar_t*))>("DLL_FileSystemDelegate_RegisterGetFileWideCallBack")(&delegate,provide);
    void* context = fn<void*(__cdecl*)(VPP)>("DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer")(&delegate);
    fn<void(__cdecl*)(void*,VPP)>("DLL_TJS_CreateTjsScriptClass")(context,&script);
    fn<void(__cdecl*)(VPP,void(__cdecl*)(const char*,std::uint8_t))>("DLL_TJS_CreateTjsConsoleClass")(&script,console);
    void* result = nullptr;
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Create")(&result);
    run("class.type","typeof Scripts",result);
    run("class.construct","typeof (new Scripts())",result);
    run("eval.scalar","Scripts.eval(\"6*7\")",result);
    run("eval.convert.source","Scripts.eval(19)",result);
    run("eval.options","Scripts.eval(\"40+2\",123,\"8\",999)",result);
    run("eval.nested","Scripts.eval(\"Scripts.eval('21*2')\")",result);
    run("eval.void","Scripts.eval(\"void\")",result);
    run("eval.object","typeof Scripts.eval(\"[1,2]\")",result);
    run("eval.array.length","Scripts.eval(\"[1,2,3]\").count",result);
    run("eval.array.join","Scripts.eval(\"[1,2,3]\").join(\",\")",result);
    run("eval.octet.length","Scripts.eval(\"<% 01 02 03 %>\").length",result);
    run("eval.invalid.source","Scripts.eval(\"1 +\")",result);
    run("eval.nested.invalid.source","Scripts.eval(\"Scripts.eval('1 +')\")",result);
    caught("eval.bad.name","Scripts.eval('42',%[])",result);
    caught("eval.bad.line","Scripts.eval('42','name',%[])",result);
    caught("exec.bad.name","Scripts.exec('var ignored=1;',%[])",result);
    caught("exec.bad.line","Scripts.exec('var ignored=1;','name',%[])",result);
    caught("eval.detached","var detached=Scripts.eval; detached('42')",result);
    run("exec.return","Scripts.exec(\"var nestedValue=14;\")",result);
    run("exec.persist","nestedValue*3",result);
    run("exec.options","Scripts.exec(\"nestedValue+=1;\",123,\"4\",999)",result);
    run("exec.persist.options","nestedValue",result);
    run("execStorage.return","Scripts.execStorage(\"exec.tjs\",123,999)",result);
    caught("execStorage.bad.name","Scripts.execStorage('exec.tjs',%[])",result);
    caught("evalStorage.bad.name","Scripts.evalStorage('eval.tjs',%[])",result);
    run("execStorage.ignored.extra","Scripts.execStorage(\"exec.tjs\",void,%[]) ",result);
    run("execStorage.persist","storageValue",result);
    run("evalStorage.path.bug","Scripts.evalStorage(\"missing.tjs\",\"eval.tjs\",999)",result);
    run("evalStorage.ignored.extra","Scripts.evalStorage(\"ignored\",\"eval.tjs\",%[]) ",result);
    run("evalStorage.omitted","Scripts.evalStorage(\"eval.tjs\")",result);
    run("evalStorage.void","Scripts.evalStorage(\"eval.tjs\",void)",result);
    run("evalStorage.numeric","Scripts.evalStorage(\"eval.tjs\",123)",result);
    run("evalStorage.cp932","Scripts.evalStorage(\"ignored\",\"cp932.tjs\")",result);
    run("evalStorage.utf8","Scripts.evalStorage(\"ignored\",\"utf8.tjs\")",result);
    run("evalStorage.utf16","Scripts.evalStorage(\"ignored\",\"utf16.tjs\")",result);
    run("execStorage.missing","Scripts.execStorage(\"missing.tjs\")",result);
    run("execStorage.empty","Scripts.execStorage(\"empty.tjs\")",result);
    run("evalStorage.empty","Scripts.evalStorage(\"ignored\",\"empty.tjs\")",result);
    const char* methods[] = {"exec","eval","execStorage","evalStorage"};
    for (auto method:methods) {
        const std::string code = std::string("var caught='none'; try { Scripts.")+method+"(); } catch(e) { caught=e.message; }";
        fn<Run>("DLL_TJS_ExecScript")(&script,code.c_str(),nullptr);
        const std::string label = std::string(method)+".noargs";
        run(label.c_str(),"caught",result);
    }
    void* second = nullptr;
    fn<void(__cdecl*)(void*,VPP)>("DLL_TJS_CreateTjsScriptClass")(context,&second);
    void* first = script;
    script = second;
    fn<void(__cdecl*)(VPP,void(__cdecl*)(const char*,std::uint8_t))>("DLL_TJS_CreateTjsConsoleClass")(&script,console);
    run("second.eval","Scripts.eval(\"7*8\")",result);
    run("second.exec","Scripts.exec(\"var nestedValue=99;\")",result);
    run("second.isolated","Scripts.eval(\"nestedValue\")",result);
    fn<void(__cdecl*)(VPP)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    script = first;
    run("first.survives","Scripts.eval(\"nestedValue\")",result);
    fn<void(__cdecl*)(VPP)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    fn<void(__cdecl*)(VPP)>("DLL_FileSystemDelegate_Delete")(&delegate);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Delete")(&result);
    FreeLibrary(module);
    return 0;
}
