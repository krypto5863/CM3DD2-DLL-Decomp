// Exported-API-only Array.sort oracle with bounded, synthetic VM inputs.
// No streams, files, KAG calls, invalid comparators, or cross-DLL C++ catches.
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
HMODULE module;
void* script;
void* result;
template<class T> T fn(const char* name) {
    auto address=GetProcAddress(module,name);
    if(!address) { std::fprintf(stderr,"missing %s\n",name); std::exit(3); }
    return reinterpret_cast<T>(address);
}
void exec(const char* source) {
    fn<void(__cdecl*)(void**,const char*,void*)>("DLL_TJS_ExecScript")(&script,source,nullptr);
}
void run(const std::string& label,const std::string& expression) {
    std::string source="var probeResult; try { probeResult = ("+expression+"); } catch(e) { probeResult = \"OUTER:\"+e.message; }";
    exec(source.c_str());
    fn<void(__cdecl*)(void**,const char*,void*)>("DLL_TJS_EvalScript")(&script,"probeResult",result);
    char buffer[16384]={};
    fn<void(__cdecl*)(void**,char*,int)>("DLL_TJSVariant_AsString")(&result,buffer,sizeof(buffer));
    std::printf("%s type=%d text=",label.c_str(),fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&result));
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(buffer);*p;++p) {
        if(*p=='\n') std::printf("\\n");
        else if(*p=='\r') std::printf("\\r");
        else if(*p=='\t') std::printf("\\t");
        else std::putchar(*p);
    }
    std::putchar('\n');
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout,nullptr,_IONBF,0);
    module=LoadLibraryA(argv[1]);
    if(!module) { std::fprintf(stderr,"LoadLibrary %lu\n",GetLastError()); return 3; }
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
    fn<void(__cdecl*)(void*,void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr,&script);
    exec("var calls=0; function asc(a,b) { calls++; return a<b; } function desc(a,b) { calls++; return a>b; } function same(a,b) { calls++; return false; } function bad(a,b) { calls++; throw new Exception(\"sort-stop\"); } function bad4(a,b) { calls++; if(calls==4) throw new Exception(\"sort-stop-four\"); return a<b; } class SortComparator { var direction; function SortComparator(d) { direction=d; } function compare(a,b) { calls++; return direction>0?a<b:a>b; } } var ascendingReceiver=new SortComparator(1); var descendingReceiver=new SortComparator(-1); var boundAsc=ascendingReceiver.compare; var boundDesc=descendingReceiver.compare; function sortProbe(a,m,noarg,stable) { calls=0; var status=\"OK\"; var rt; try { if(stable===void) { if(noarg) rt=a.sort(); else rt=a.sort(m); } else rt=a.sort(noarg?void:m,stable); status += \":\"+typeof rt; } catch(e) { status=\"ERROR:\"+e.message; } return status+\"|calls=\"+calls+\"|count=\"+a.count+\"|\"+a.join(\",\"); } function keys() { var d=%[\"z\"=>3,\"a\"=>1,\"middle\"=>2]; return (Dictionary.getKeyArray incontextof d)(); } function makeCount(n) { var a=[]; for(var i=0;i<n;i++) a.add((i*17)%33); return a; }");
    struct Input { const char* label; const char* expression; };
    const Input inputs[]={
        {"empty","[]"},{"one","[3]"},{"two","[3,1]"},
        {"numbers","[3,1,2,1,10]"},{"strings","[\"z\",\"a\",\"middle\"]"},
        {"numericStrings","[\"10\",\"2\",\"1\"]"},
        {"mixed","[3,\"10\",2,\"1\"]"},{"keys","keys()"},
        // Keep boundary probes within the recovered small-sort range.
        {"boundary31","makeCount(31)"},{"boundary32","makeCount(32)"}
    };
    const Input modes[]={
        {"omitted","void"},{"void","void"},{"a","\"a\""},{"d","\"d\""},
        {"n","\"n\""},{"N","\"N\""},{"zero","0"},{"one","1"},
        {"empty","\"\""},{"unknown","\"unknown\""},
        {"plus","\"+\""},{"minus","\"-\""},
        {"stringZero","\"0\""},{"stringNine","\"9\""},{"z","\"z\""},
        {"ascending","asc"},{"descending","desc"},{"equal","same"},{"throws","bad"},
        {"throwsFourth","bad4"},{"boundAscending","boundAsc"},{"boundDescending","boundDesc"}
    };
    const Input stability[]={{"default","void"},{"unstable","false"},{"stable","true"}};
    for(const auto& input:inputs) for(const auto& mode:modes) for(const auto& stable:stability) {
        run(std::string(input.label)+"."+mode.label+"."+stable.label,
            "sortProbe("+std::string(input.expression)+","+mode.expression+","+
            (std::string(mode.label)=="omitted"?"true":"false")+","+stable.expression+")");
    }
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
    fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    FreeLibrary(module);
    return 0;
}
