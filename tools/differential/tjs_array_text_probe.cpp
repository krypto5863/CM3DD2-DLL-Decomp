// Synthetic split/join and safe stream-precondition oracle. No storage calls
// reach a file-stream path: filenames appear only in Dictionary no-op methods.
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
HMODULE module;
void* script;
void* result;
template<class T> T fn(const char* name) {
    auto p=GetProcAddress(module,name);
    if(!p) { std::fprintf(stderr,"missing %s\n",name); std::exit(3); }
    return reinterpret_cast<T>(p);
}
void exec(const std::string& code) {
    fn<void(__cdecl*)(void**,const char*,void*)>("DLL_TJS_ExecScript")(&script,code.c_str(),nullptr);
}
void run(const std::string& label,const std::string& expression) {
    exec("var answer; try { answer=("+expression+"); } catch(e) { answer=\"ERROR:\"+e.message; }");
    fn<void(__cdecl*)(void**,const char*,void*)>("DLL_TJS_EvalScript")(&script,"answer",result);
    char text[8192]={};
    fn<void(__cdecl*)(void**,char*,int)>("DLL_TJSVariant_AsString")(&result,text,sizeof(text));
    std::printf("%s type=%d utf8=",label.c_str(),fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&result));
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p) std::printf("%02x",unsigned(*p));
    std::putchar('\n');
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    module=LoadLibraryA(argv[1]); if(!module) return 3;
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
    fn<void(__cdecl*)(void*,void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr,&script);
    exec("function shape(a) { var s=\"count=\"+a.count; for(var i=0;i<a.count;i++) s+=\"|\"+typeof a[i]+\":\"+a[i]; return s; } function splitProbe(p,s,purge) { var a=[\"old\",123]; var state=\"OK\"; var r; try { r=a.split(p,s,void,purge); state+=\":\"+(r===a); } catch(e) { state=\"ERROR:\"+e.message; } return state+\"|\"+shape(a); } function joinProbe(a,p,purge) { return a.join(p,void,purge); }");
    struct Item { const char* label; const char* value; };
    const Item patterns[]={{"comma","\",\""},{"set","\",;\""},{"empty","\"\""},{"void","void"},{"number","12"}};
    const Item strings[]={{"empty","\"\""},{"plain","\"alpha\""},{"ends","\",alpha,,beta,\""},
        {"mixed","\"a;b,c;;\""},{"number","121212"},{"void","void"}};
    for(const auto& p:patterns) for(const auto& s:strings) for(const auto& purge: {"false","true"})
        run(std::string("split.")+p.label+"."+s.label+"."+purge,
            std::string("splitProbe(")+p.value+","+s.value+","+purge+")");
    run("split.unicode","splitProbe(\"、。\",\"、猫。犬、、\",false)");
    run("split.source-octet-failure","splitProbe(\",\",<%00 01%>,false)");
    run("split.pattern-octet-failure","splitProbe(<%00 01%>,\"a,b\",false)");
    exec("var a=[\"old\"]; var caught=\"\"; try { a.split(\",\"); } catch(e) { caught=e.message; }");
    run("split.short-preserves","caught+\"|\"+shape(a)");
    exec("a=[\"old\"]; a.split(\",\",\"a,,b\",<%00%>,false,99);");
    run("split.reserved-extra","shape(a)");
    exec("a=[\"old\"]; a.split(\",\",\"a,,b\",true);");
    run("split.third-is-reserved","shape(a)");
    exec("a=[\"old\"]; a.split(\",\",\"a,,b\");");
    run("split.no-result","shape(a)");
    run("split.escaped-zero","splitProbe(\",\",\"a\\0b,c\",false)");
    run("string.hex-zero-length","\"a\\x0000b\".length");
    run("split.hex-zero","splitProbe(\",\",\"a\\x0000b,c\",false)");
    const Item arrays[]={{"empty","[]"},{"text","[\"a\",\"\",\"b\"]"},
        {"voids","[void,\"a\",void,\"\",void,0,false]"},{"numbers","[1,2.5,-3]"}};
    const Item delimiters[]={{"pipe","\"|\""},{"empty","\"\""},{"void","void"},{"number","12"}};
    for(const auto& a:arrays) for(const auto& d:delimiters) for(const auto& purge: {"false","true"})
        run(std::string("join.")+a.label+"."+d.label+"."+purge,
            std::string("joinProbe(")+a.value+","+d.value+","+purge+")");
    run("join.octet-element","joinProbe([\"a\",<%00%>],\"|\",false)");
    run("join.octet-delimiter","joinProbe([],<%00%>,false)");
    run("join.third-truthy-string","joinProbe([void,\"\",\"b\"],\"|\",\"1\")");
    run("join.third-zero-string","joinProbe([void,\"\",\"b\"],\"|\",\"0\")");
    run("join.short","[].join()");
    run("join.wrong-receiver","(Array.join incontextof %[]) (\"|\")");
    run("split.wrong-receiver","(Array.split incontextof %[]) (\",\",\"x,y\")");
    exec("a=[\"a\",void,\"b\"]; var f=a.join;");
    run("join.bound","f(\"|\")");
    run("join.rebound","(f incontextof [1,2])(\"|\")");
    exec("invalidate a;");
    run("join.invalidated","f(\"|\")");
    exec("a=[1,2]; var splitBound=a.split; invalidate a;");
    run("split.invalidated-bound","(splitBound(\",\",\"x,y\")===a)");
    run("join.invalidated-refilled","(Array.join incontextof a)(\"|\")");
    run("join.escaped-zero-length","[\"a\\0b\",\"c\"].join(\"|\").length");
    run("join.hex-zero-length","[\"a\\x0000b\",\"c\"].join(\"|\").length");
    run("array.load.noargs","[].load()");
    run("array.save.noargs","[].save()");
    run("array.saveStruct.noargs","[].saveStruct()");
    exec("var d=%[\"sentinel\"=>7];");
    run("dictionary.load.noop","typeof (Dictionary.load incontextof d)()");
    run("dictionary.save.noop","typeof (Dictionary.save incontextof d)()");
    run("dictionary.load.ignored","typeof (Dictionary.load incontextof d)(<%00%>,<%01%>,123)");
    run("dictionary.save.ignored","typeof (Dictionary.save incontextof d)(<%00%>,<%01%>,123)");
    run("dictionary.stub-preserves","d.sentinel");
    run("dictionary.saveStruct.noargs","(Dictionary.saveStruct incontextof d)()");
    run("dictionary.load.wrong-receiver","(Dictionary.load incontextof [])()");
    run("dictionary.save.wrong-receiver","(Dictionary.save incontextof [])()");
    exec("var savedLoad=Dictionary.load incontextof d; invalidate d;");
    run("dictionary.load.invalidated","savedLoad()");
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
    fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    FreeLibrary(module);
    return 0;
}
