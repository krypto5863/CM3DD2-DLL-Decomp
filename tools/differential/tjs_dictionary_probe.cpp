// API-only synthetic Dictionary oracle. Never invokes file-stream methods.
#include <Windows.h>
#include <cstdint>
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
void run(const char* label,const char* expression) {
    // Keep errors inside the VM, never catch foreign DLL C++ exception objects.
    std::string source="var probeResult; try { probeResult = (";
    source+=expression;
    source+="); } catch(probeError) { probeResult = \"ERROR:\" + probeError.message; }";
    exec(source.c_str());
    fn<void(__cdecl*)(void**,const char*,void*)>("DLL_TJS_EvalScript")(&script,"probeResult",result);
    char buffer[16384]={};
    fn<void(__cdecl*)(void**,char*,int)>("DLL_TJSVariant_AsString")(&result,buffer,sizeof(buffer));
    std::printf("%s type=%d text=",label,fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&result));
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
    exec("var empty = %[]; var one = %[\"a\"=>1]; var multi = %[\"z\"=>3,\"a\"=>1,\"middle\"=>2]; function has(o,n) { return typeof o[n]; } function struct(d) { return (Dictionary.toStructString incontextof d)(); } function keys(d) { return (Dictionary.getKeyArray incontextof d)(); } function sortedKeys(d) { var a=keys(d); a.sort(\"a\"); return a.join(\"|\"); } function rawKeys(d) { return keys(d).join(\"|\"); } function roundtrip(d) { var s=struct(d); var v=Scripts.eval(s); return struct(v); }");
    run("method.toStructString.class","has(Dictionary,\"toStructString\")");
    run("method.toStructString.instance","has(empty,\"toStructString\")");
    run("method.getKeyArray.class","has(Dictionary,\"getKeyArray\")");
    run("method.getKeyArray.instance","has(empty,\"getKeyArray\")");
    run("method.loadStruct.class","has(Dictionary,\"loadStruct\")");
    run("method.loadStruct.instance","has(empty,\"loadStruct\")");
    run("struct.empty","struct(empty)");
    run("struct.one","struct(one)");
    run("struct.multi","struct(multi)");
    run("struct.args.ignored","(Dictionary.toStructString incontextof one)(1,\"unused\",null)");
    run("keys.empty","rawKeys(empty)");
    run("keys.one","rawKeys(one)");
    run("keys.multi.raw","rawKeys(multi)");
    run("keys.multi.count","keys(multi).count");
    run("keys.args.ignored","(Dictionary.getKeyArray incontextof multi)(1,null).join(\"|\")");
    run("keys.array.type","keys(multi) instanceof Array");
    // Native compact serialization dereferences null object closures unchecked.
    // Do not exercise null objects or cycles in the native process.
    exec("var scalar = %[\"void\"=>void,\"negative\"=>-42,\"real\"=>1.25,\"boolean\"=>true,\"text\"=>\"quote\\\" slash\\\\ tab\\t newline\\n carriage\\r\",\"octet\"=><% 00 7f 80 ff %>];");
    run("struct.scalars","struct(scalar)");
    run("keys.scalars.ordered","rawKeys(scalar)");
    exec("var nested = %[\"dictionary\"=>%[\"x\"=>7],\"array\"=>[1,\"two\",void],\"emptyArray\"=>[],\"emptyDictionary\"=>%[]];");
    run("struct.nested","struct(nested)");
    run("struct.roundtrip.one","roundtrip(one)");
    run("struct.roundtrip.scalars","roundtrip(scalar)");
    run("struct.roundtrip.nested","roundtrip(nested)");
    exec("class Example { var value=9; function f() { return value; } } var instance=new Example(); var objects=%[\"instance\"=>instance,\"function\"=>instance.f,\"builtin\"=>Dictionary];");
    run("struct.objects","struct(objects)");
    run("keys.objects.ordered","rawKeys(objects)");
    exec("var weirdKeys=%[\"q\\\"\\\\\\n\"=>1,\"日本語\"=>2,\"1\"=>3];");
    run("struct.keys.escaping","struct(weirdKeys)");
    run("keys.escaping.ordered","rawKeys(weirdKeys)");
    exec("var method=Dictionary.toStructString incontextof one; var keyMethod=Dictionary.getKeyArray incontextof one;");
    run("context.bound.struct","method()");
    run("context.bound.keys","keyMethod().join(\"|\")");
    run("context.class.struct","Dictionary.toStructString()");
    run("context.class.keys","Dictionary.getKeyArray().length");
    run("context.rebound.struct","(method incontextof multi)()");
    run("context.rebound.keys","(keyMethod incontextof multi)().join(\"|\")");
    run("context.invalid.struct","(method incontextof instance)()");
    run("context.invalid.keys","(keyMethod incontextof instance)().length");
    // Do not invoke load/save/saveStruct: native stream factories are NULL.
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
    fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    FreeLibrary(module);
    return 0;
}
