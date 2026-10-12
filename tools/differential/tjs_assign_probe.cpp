// Bounded synthetic assign/assignStruct oracle: no files or arbitrary host
// objects. Cycles/nulls are checked only through identity, never serialized.
// Dictionary serialization is class-bound explicitly.
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
HMODULE module;
void *script, *result;
template<class T> T fn(const char* name) {
    auto p = GetProcAddress(module, name);
    if(!p) { std::fprintf(stderr, "missing %s\n", name); std::exit(3); }
    return reinterpret_cast<T>(p);
}
void exec(const std::string& code) {
    fn<void(__cdecl*)(void**,const char*,void*)>("DLL_TJS_ExecScript")(&script,code.c_str(),nullptr);
}
void run(const std::string& label,const std::string& body) {
    exec("function assignCase(){"+body+"} var answer; try {answer=assignCase();} catch(e){answer=\"ERROR:\"+e.message;}");
    fn<void(__cdecl*)(void**,const char*,void*)>("DLL_TJS_EvalScript")(&script,"answer",result);
    char buffer[16384]={};
    fn<void(__cdecl*)(void**,char*,int)>("DLL_TJSVariant_AsString")(&result,buffer,sizeof(buffer));
    std::printf("%s type=%d utf8=",label.c_str(),fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&result));
    for(auto p=reinterpret_cast<unsigned char*>(buffer);*p;++p) std::printf("%02x",unsigned(*p));
    std::putchar('\n'); std::fflush(stdout);
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    module=LoadLibraryA(argv[1]); if(!module) return 3;
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
    fn<void(__cdecl*)(void*,void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr,&script);
    exec("function dictText(d){return (Dictionary.toStructString incontextof d)();} function arrayText(a){return dictText(%[\"array\"=>a]);} class AssignSource { var alpha=11; var beta=22; function ignored(){return 9;} }");
    struct Item { const char* label; const char* expression; };
    const Item sources[]={{"empty-array","[]"},{"numbers","[1,2,3]"},
        {"pairs","[\"alpha\",11,\"beta\",22]"},{"odd-tail","[\"alpha\",11,\"tail\"]"},
        {"duplicate-key","[\"alpha\",11,\"alpha\",22]"},{"numeric-key","[7,11,8,22]"},
        {"empty-dictionary","%[]"},{"dictionary","%[\"alpha\"=>11,\"beta\"=>22]"}};
    for(const auto& method: {"assign","assignStruct"}) {
        for(const auto& src:sources) {
            run(std::string("array.")+method+"."+src.label,
                std::string("var a=[\"old\",99]; var s=")+src.expression+"; var r=a."+method+"(s); return typeof r+\"|\"+arrayText(a);");
            run(std::string("dictionary.")+method+"."+src.label,
                std::string("var d=%[\"old\"=>99]; var s=")+src.expression+"; var r=(Dictionary."+method+" incontextof d)(s); return typeof r+\"|\"+dictText(d);");
        }
        for(const auto& bad: {"void","17","\"text\"","<%00 01%>"}) {
            run(std::string("array.")+method+".bad."+bad,
                std::string("var a=[\"old\",99]; var status=\"OK\"; try {a.")+method+"("+bad+");} catch(e){status=e.message;} return status+\"|\"+arrayText(a);");
            run(std::string("dictionary.")+method+".bad."+bad,
                std::string("var d=%[\"old\"=>99]; var status=\"OK\"; try {(Dictionary.")+method+" incontextof d)("+bad+");} catch(e){status=e.message;} return status+\"|\"+dictText(d);");
        }
        run(std::string("array.")+method+".noargs",std::string("var a=[9]; var status=\"OK\"; try {a.")+method+"();} catch(e){status=e.message;} return status+\"|\"+arrayText(a);");
        run(std::string("dictionary.")+method+".noargs",std::string("var d=%[\"old\"=>99]; var status=\"OK\"; try {(Dictionary.")+method+" incontextof d)();} catch(e){status=e.message;} return status+\"|\"+dictText(d);");
        run(std::string("array.")+method+".wrong-receiver",std::string("return (Array.")+method+" incontextof %[])([]);");
        run(std::string("dictionary.")+method+".wrong-receiver",std::string("return (Dictionary.")+method+" incontextof [])(%[]);");
        run(std::string("dictionary.")+method+".instance-registration",std::string("return typeof %[].")+method+";");
        run(std::string("array.")+method+".class-registration",std::string("return typeof Array.")+method+";");
        run(std::string("array.")+method+".bound-source",std::string("var s=[1,2,3]; var a=[9]; a.")+method+"(s.join); return arrayText(a);");
        run(std::string("dictionary.")+method+".bound-source",std::string("var s=%[\"alpha\"=>11]; var f=Dictionary.assign incontextof s; var d=%[]; (Dictionary.")+method+" incontextof d)(f); return dictText(d);");
        run(std::string("array.")+method+".rebound-source",std::string("var s=[1,2,3]; var a=[9]; a.")+method+"(s.join incontextof [7,8]); return arrayText(a);");
        run(std::string("dictionary.")+method+".generic-class",std::string("var s=new AssignSource(); var d=%[]; (Dictionary.")+method+" incontextof d)(s); return d.alpha+\"|\"+d.beta+\"|\"+typeof d.ignored;");
        run(std::string("array.")+method+".generic-class",std::string("var s=new AssignSource(); var a=[]; a.")+method+"(s); return a.count+\"|\"+typeof a[1]+\"|\"+typeof a[3]+\"|\"+typeof a[5];");
        run(std::string("array.")+method+".nested",std::string("var inner=[1,%[\"x\"=>2]]; var s=[inner,inner,<%00 7f%>]; var a=[]; a.")+method+"(s); a[0][0]=7; return (a[0]===inner)+\"|\"+(a[0]===a[1])+\"|\"+inner[0]+\"|\"+arrayText(a);");
        run(std::string("dictionary.")+method+".nested",std::string("var inner=%[\"x\"=>[1,2]]; var s=%[\"a\"=>inner,\"b\"=>inner]; var d=%[]; (Dictionary.")+method+" incontextof d)(s); d.a.x[0]=7; return (d.a===inner)+\"|\"+(d.a===d.b)+\"|\"+inner.x[0]+\"|\"+dictText(d);");
        run(std::string("array.")+method+".extra-ignored",std::string("var a=[9]; a.")+method+"([1,2],<%00%>,33); return arrayText(a);");
        run(std::string("dictionary.")+method+".extra",std::string("var d=%[\"old\"=>99]; (Dictionary.")+method+" incontextof d)(%[\"alpha\"=>11],false,33); return dictText(d);");
    }
    for(const auto& flag: {"void","false","true","0","1","\"0\"","\"1\"","<%00%>"})
        run(std::string("dictionary.assign.clear.")+flag,std::string("var d=%[\"old\"=>99,\"alpha\"=>0]; var status=\"OK\"; try {(Dictionary.assign incontextof d)(%[\"alpha\"=>11,\"beta\"=>22],")+flag+");} catch(e){status=e.message;} return status+\"|\"+dictText(d);");
    run("array.assign.self","var a=[1,2,3]; a.assign(a); return arrayText(a);");
    run("dictionary.assign.self-clear","var d=%[\"alpha\"=>11]; (Dictionary.assign incontextof d)(d); return dictText(d);");
    run("dictionary.assign.self-merge","var d=%[\"alpha\"=>11]; (Dictionary.assign incontextof d)(d,false); return dictText(d);");
    run("array.assignStruct.self","var a=[1,2,3]; a.assignStruct(a); return arrayText(a);");
    run("dictionary.assignStruct.self","var d=%[\"alpha\"=>11]; (Dictionary.assignStruct incontextof d)(d); return dictText(d);");
    // These structured cycles are explicitly supported by native active-path
    // checks. Do not serialize null object values: native serializer is unsafe.
    run("array.assignStruct.cycle","var s=[]; s[0]=s; var a=[]; a.assignStruct(s); return a.count+\"|\"+(a[0]===null);");
    run("dictionary.assignStruct.cycle","var s=%[]; s.x=s; var d=%[]; (Dictionary.assignStruct incontextof d)(s); return (Dictionary.getKeyArray incontextof d)().count+\"|\"+(d.x===null);");
    run("array.assignStruct.mixed-cycle","var s=[]; var d=%[\"back\"=>s]; s[0]=d; var a=[]; a.assignStruct(s); return (a[0]!==d)+\"|\"+(a[0].back===null);");
    run("dictionary.assignStruct.mixed-cycle","var s=%[]; var a=[s]; s.x=a; var d=%[]; (Dictionary.assignStruct incontextof d)(s); return (d.x!==a)+\"|\"+(d.x[0]===null);");
    run("array.assignStruct.generic-posterror","var a=[9]; var status=\"OK\"; try {a.assignStruct(new AssignSource());} catch(e){status=e.message;} return status+\"|\"+arrayText(a);");
    run("dictionary.assignStruct.generic-posterror","var d=%[\"old\"=>99]; var status=\"OK\"; try {(Dictionary.assignStruct incontextof d)(new AssignSource());} catch(e){status=e.message;} return status+\"|\"+dictText(d);");
    run("array.assign.null-posterror","var a=[9]; var status=\"OK\"; try {a.assign(null);} catch(e){status=e.message;} return status+\"|\"+arrayText(a);");
    run("dictionary.assign.null-posterror","var d=%[\"old\"=>99]; var status=\"OK\"; try {(Dictionary.assign incontextof d)(null);} catch(e){status=e.message;} return status+\"|\"+dictText(d);");
    for(int size: {15,20,32,64,129}) {
        std::string setup="var s=%[];";
        for(int i=0;i<size;++i) setup+="s[\"k"+std::to_string(i)+"\"]="+std::to_string(i)+";";
        run("array.assign.large."+std::to_string(size),setup+"var a=[]; a.assign(s); return arrayText(a);");
        run("dictionary.assign.large."+std::to_string(size),setup+"var d=%[]; (Dictionary.assign incontextof d)(s); return dictText(d);");
        run("dictionary.assignStruct.large."+std::to_string(size),setup+"var d=%[]; (Dictionary.assignStruct incontextof d)(s); return dictText(d);");
    }
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
    fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    FreeLibrary(module);
    return 0;
}
