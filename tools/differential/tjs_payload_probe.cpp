// Synthetic payload/ownership oracle. Each DLL runs in a separate process.
// Only exported APIs; never copy layouts or Delete callback-borrowed variants.
// Octets cannot be converted to strings by tTJSVariant::AsString, so inspect
// their length/bytes within the VM instead of triggering foreign exceptions.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
HMODULE module;
void* script;
void* retained;
char rejected_string_buffer[128];
using VPP = void**;
using Callback = void(__cdecl*)(const void*, void*);
template<class T> T fn(const char* name) {
    auto p = GetProcAddress(module,name);
    if(!p) { std::fprintf(stderr,"Missing export: %s\n",name); std::exit(3); }
    return reinterpret_cast<T>(p);
}
void* reference(void* raw) {
    void* v=nullptr;
    fn<void(__cdecl*)(VPP,void*)>("DLL_TJSVariant_Reference")(&v,raw);
    return v;
}
int type(void* raw) {
    auto v=reference(raw);
    return fn<int(__cdecl*)(VPP)>("DLL_TJSVariant_GetType")(&v);
}
std::string normalize(const char* s) {
    std::string out;
    for(const char* p=s; *p;) {
        if(p[0]=='0' && p[1]=='x') {
            const char* q=p+2;
            while((*q>='0'&&*q<='9')||(*q>='a'&&*q<='f')||(*q>='A'&&*q<='F')) ++q;
            if(q-p>=10) { out+="0xADDR"; p=q; continue; }
        }
        out+=*p++;
    }
    return out;
}
void observe(const char* label,void* raw) {
    auto v=reference(raw);
    int t=type(v);
    std::printf("%s type=%d",label,t);
    if(t!=3) {
        char text[1024]={};
        fn<void(__cdecl*)(VPP,char*,int)>("DLL_TJSVariant_AsString")(&v,text,sizeof(text));
        std::printf(" text=%s",normalize(text).c_str());
    }
    std::printf("\n");
}
void eval(const char* code,void* result) {
    fn<void(__cdecl*)(VPP,const char*,void*)>("DLL_TJS_EvalScript")(&script,code,result);
}
void exec(const char* code) {
    fn<void(__cdecl*)(VPP,const char*,void*)>("DLL_TJS_ExecScript")(&script,code,nullptr);
}
void run(const char* label,const char* code,void* result) {
    eval(code,result); observe(label,result);
}
void __cdecl console(const char* message,std::uint8_t exception) {
    std::printf("console exception=%u text=%s\n",unsigned(exception),normalize(message).c_str());
}
void __cdecl inspect(const void* params,void* result) {
    int n=fn<int(__cdecl*)(const void*)>("DLL_TJS_ParamAccess_Size")(params);
    std::printf("inspect count=%d result=%d\n",n,result!=nullptr);
    for(int i=0;i<n;++i) {
        char label[32]; std::snprintf(label,sizeof(label),"inspect.arg%d",i);
        observe(label,fn<void*(__cdecl*)(const void*,int)>("DLL_TJS_ParamAccess_At")(params,i));
    }
    if(result) eval("17",result);
}
void __cdecl objectReturn(const void*,void* result) {
    std::printf("object.return result=%d\n",result!=nullptr);
    if(result) eval("savedObject",result);
}
void __cdecl closureReturn(const void*,void* result) {
    if(result) eval("savedClosure",result);
}
void __cdecl octetReturn(const void*,void* result) {
    if(result) eval("savedOctet",result);
}
void __cdecl mutate(const void* params,void* result) {
    auto raw=fn<void*(__cdecl*)(const void*,int)>("DLL_TJS_ParamAccess_At")(params,0);
    auto a=reference(raw),b=reference(raw);
    std::printf("mutate aliases=%d\n",a==b);
    fn<void(__cdecl*)(VPP,const char*)>("DLL_TJSVariant_SetString")(&a,"changed");
    observe("mutate.alias",b);
    auto again=fn<void*(__cdecl*)(const void*,int)>("DLL_TJS_ParamAccess_At")(params,0);
    observe("mutate.at",again);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Clear")(&a);
    observe("mutate.clear",b);
    if(result) {
        eval("savedObject",result);
        observe("mutate.after.reentry",b);
    }
}
void __cdecl rejectInteger(const void* params,void*) {
    auto v=reference(fn<void*(__cdecl*)(const void*,int)>("DLL_TJS_ParamAccess_At")(params,0));
    (void)fn<std::int64_t(__cdecl*)(VPP)>("DLL_TJSVariant_AsInteger")(&v);
}
void __cdecl rejectReal(const void* params,void*) {
    auto v=reference(fn<void*(__cdecl*)(const void*,int)>("DLL_TJS_ParamAccess_At")(params,0));
    (void)fn<float(__cdecl*)(VPP)>("DLL_TJSVariant_AsReal")(&v);
}
void __cdecl rejectString(const void* params,void*) {
    auto v=reference(fn<void*(__cdecl*)(const void*,int)>("DLL_TJS_ParamAccess_At")(params,0));
    std::memset(rejected_string_buffer,0x5a,sizeof(rejected_string_buffer));
    fn<void(__cdecl*)(VPP,char*,int)>("DLL_TJSVariant_AsString")(&v,rejected_string_buffer,sizeof(rejected_string_buffer));
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc<2 || argc>3) return 2;
    module=LoadLibraryA(argv[1]);
    if(!module) { std::fprintf(stderr,"LoadLibrary: %lu\n",GetLastError()); return 3; }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    void* result=nullptr;
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Create")(&result);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Create")(&retained);
    fn<void(__cdecl*)(void*,VPP)>("DLL_TJS_CreateTjsScriptClass")(nullptr,&script);
    fn<void(__cdecl*)(VPP,void(__cdecl*)(const char*,std::uint8_t))>("DLL_TJS_CreateTjsConsoleClass")(&script,console);
    auto add=fn<void(__cdecl*)(VPP,const char*,Callback)>("DLL_TJS_AddMemberFunction");
    add(&script,"inspect",inspect); add(&script,"objectReturn",objectReturn);
    add(&script,"closureReturn",closureReturn); add(&script,"octetReturn",octetReturn);
    add(&script,"mutate",mutate);
    add(&script,"rejectInteger",rejectInteger); add(&script,"rejectReal",rejectReal);
    add(&script,"rejectString",rejectString);
    try {
        exec("var savedObject = %[\"value\"=>21]; var savedOctet = <% 00 7f 80 ff %>; class Receiver { var value = 41; function get() { return value; } } var receiver = new Receiver(); var savedClosure = receiver.get;");
        run("object.direct","savedObject",result);
        run("closure.direct","savedClosure",result);
        run("octet.direct","savedOctet",result);
        run("inspect.result","inspect(savedObject,savedClosure,savedOctet)",result);
        run("object.identity","objectReturn() === savedObject",result);
        exec("objectReturn().value = 93;");
        run("object.mutation","savedObject.value",result);
        run("closure.identity","closureReturn() === savedClosure",result);
        run("closure.receiver","closureReturn()()",result);
        exec("receiver.value = 57;");
        run("closure.changed.receiver","closureReturn()()",result);
        run("octet.identity","octetReturn() === savedOctet",result);
        exec("var b = octetReturn();");
        run("octet.bytes","b.length + \":\" + b[0] + \":\" + b[1] + \":\" + b[2] + \":\" + b[3]",result);
        run("mutation.result","mutate(savedObject)",result);
        run("mutation.global","savedObject.value",result);
        // Foreign conversion exceptions unwind only into their owning VM's
        // host-call boundary and are caught by script, never across main().
        exec("var caught = \"\"; try { rejectInteger(savedObject); } catch(e) { caught = e.message; }");
        run("object.integer.rejected","caught",result);
        exec("caught = \"\"; try { rejectReal(savedObject); } catch(e) { caught = e.message; }");
        run("object.real.rejected","caught",result);
        exec("caught = \"\"; try { rejectInteger(savedOctet); } catch(e) { caught = e.message; }");
        run("octet.integer.rejected","caught",result);
        exec("caught = \"\"; try { rejectReal(savedOctet); } catch(e) { caught = e.message; }");
        run("octet.real.rejected","caught",result);
        exec("caught = \"\"; try { rejectString(savedOctet); } catch(e) { caught = e.message; }");
        run("octet.string.rejected","caught",result);
        bool destination_unchanged=true;
        for(char byte:rejected_string_buffer) destination_unchanged &= byte==0x5a;
        std::printf("octet.string.destination.unchanged=%d\n",destination_unchanged);
        eval("%[\"alive\"=>1]",retained);
        fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Clear")(&result);
        // Primary probe releases every owned payload before engine teardown.
        // Optional "retain" is bounded to a plain Dictionary with no finalize
        // hook: objects that execute script during teardown are not safe here.
        if(argc!=3 || std::strcmp(argv[2],"retain")!=0) {
            fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Clear")(&retained);
            observe("object.before.script.delete",retained);
        }
        fn<void(__cdecl*)(VPP)>("DLL_TJS_DeleteTjsScriptClass")(&script);
        std::printf("script.deleted=%d\n",script==nullptr);
        observe("object.after.script.delete",retained);
        fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Clear")(&retained);
        observe("object.after.clear",retained);
    } catch(...) { std::printf("exception.escaped\n"); }
    if(script) fn<void(__cdecl*)(VPP)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Delete")(&retained);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Delete")(&result);
    FreeLibrary(module);
}
