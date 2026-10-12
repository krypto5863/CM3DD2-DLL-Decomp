// Controlled RegExp characterization through exported APIs only.
// Run each DLL in an independent process; no file/network script operations.
// This oracle is bounded runtime evidence, not a replacement for decompilation.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
HMODULE module;
void* script;
void* result;
unsigned console_events;
using VPP = void**;
template<class T> T fn(const char* name) {
    auto p = GetProcAddress(module, name);
    if(!p) { std::fprintf(stderr, "Missing export: %s\n", name); std::exit(3); }
    return reinterpret_cast<T>(p);
}
std::string escaped(const char* text) {
    std::string out;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if(*p=='\n') out+="\\n";
        else if(*p=='\r') out+="\\r";
        else if(*p=='\t') out+="\\t";
        else out+=char(*p);
    }
    return out;
}
void __cdecl console(const char*, std::uint8_t) { ++console_events; }
void exec(const char* code) {
    fn<void(__cdecl*)(VPP,const char*,void*)>("DLL_TJS_ExecScript")(&script,code,nullptr);
}
void run(const char* name,const char* expression) {
    std::string code="__probe = \"UNASSIGNED\"; try { __probe = (";
    code+=expression;
    code+="); } catch(e) { __probe = \"ERROR:\" + e.message; }";
    unsigned before=console_events;
    exec(code.c_str());
    fn<void(__cdecl*)(VPP,const char*,void*)>("DLL_TJS_EvalScript")(&script,"__probe",result);
    char text[4096]={};
    fn<void(__cdecl*)(VPP,char*,int)>("DLL_TJSVariant_AsString")(&result,text,sizeof(text));
    int type=fn<int(__cdecl*)(VPP)>("DLL_TJSVariant_GetType")(&result);
    std::printf("%s type=%d text=%s console=%u\n",name,type,escaped(text).c_str(),console_events-before);
}
void action(const char* code) {
    std::string guarded="try { "; guarded+=code;
    guarded+=" } catch(e) { __setupError = e.message; }";
    exec(guarded.c_str());
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc!=2) return 2;
    module=LoadLibraryA(argv[1]);
    if(!module) { std::fprintf(stderr,"LoadLibrary: %lu\n",GetLastError()); return 3; }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Create")(&result);
    fn<void(__cdecl*)(void*,VPP)>("DLL_TJS_CreateTjsScriptClass")(nullptr,&script);
    fn<void(__cdecl*)(VPP,void(__cdecl*)(const char*,std::uint8_t))>("DLL_TJS_CreateTjsConsoleClass")(&script,console);
    exec("var __probe; var __setupError = \"\"; function arr(a) { return a.count + \":\" + a.join(\"|\"); } function state(r) { return r.start + \":\" + r.index + \":\" + r.lastIndex + \":\" + r.input + \":\" + r.lastMatch + \":\" + r.lastParen + \":\" + r.leftContext + \":\" + r.rightContext; } var r; var a; var calls=0;");
    run("class.present","typeof RegExp");
    run("constructor.basic","new RegExp(\"ab\").test(\"zabc\")");
    run("literal.basic","/ab/.test(\"zabc\")");
    run("literal.ignorecase","/ab/i.test(\"zABc\")");
    run("constructor.ignorecase","new RegExp(\"ab\",\"i\").test(\"zABc\")");
    run("flags.unknown","new RegExp(\"ab\",\"xyz\").test(\"zabc\")");
    run("flags.localized","new RegExp(\"ab\",\"l\").test(\"zabc\")");
    run("flags.slash.terminates","new RegExp(\"ab\",\"/i\").test(\"AB\")");
    run("capture.match","arr(new RegExp(\"(a)(b+)\").match(\"zabbbc\"))");
    run("capture.optional","arr(new RegExp(\"(a)(b)?\").match(\"zac\"))");
    run("capture.none","arr(new RegExp(\"a\").match(\"xyz\"))");
    action("r = new RegExp(\"(a)(b+)\",\"g\");");
    run("state.initial","state(r)");
    run("global.exec.first","arr(r.exec(\"zabbbxab\"))");
    run("state.first","state(r)");
    run("global.exec.second","arr(r.exec(\"zabbbxab\"))");
    run("state.second","state(r)");
    run("global.exec.failed","arr(r.exec(\"zabbbxab\"))");
    run("state.failed","state(r)");
    run("global.last.identity","RegExp.last === r");
    run("matches.property","arr(r.matches)");
    action("r.start = 1;");
    run("match.nonmutating","arr(r.match(\"zabbbxab\"))");
    run("state.after.match","state(r)");
    run("test.mutating","r.test(\"zabbbxab\")");
    run("state.after.test","state(r)");
    action("r.start = 99;");
    run("start.outside.match","arr(r.match(\"abc\"))");
    // Do not exec/test an out-of-range start: unknown native implementations
    // might build leftContext with an unchecked length. Match alone is bounded.
    action("r = new RegExp(\"a\"); r.start=1;");
    run("nonglobal.exec","arr(r.exec(\"zaaz\"))");
    run("nonglobal.state","state(r)");
    run("nonglobal.exec.repeat","arr(r.exec(\"zaaz\"))");
    run("replace.first","new RegExp(\"a\").replace(\"aba\",\"X\")");
    run("replace.global","new RegExp(\"a\",\"g\").replace(\"aba\",\"X\")");
    run("replace.literal.dollar","new RegExp(\"(a)\",\"g\").replace(\"aba\",\"$1:$&\")");
    run("replace.callback","new RegExp(\"(a)(b?)\",\"g\").replace(\"ab-a\",function(a) { calls++; return \"[\"+arr(a)+\"]\"; })");
    run("replace.callback.count","calls");
    run("replace.string.method","\"aba\".replace(/a/g,\"X\")");
    run("replace.state.unchanged","state(r)");
    run("split.keepempty","arr(new RegExp(\",\").split(\",a,,b,\"))");
    run("split.purgeempty","arr(new RegExp(\",\").split(\",a,,b,\",void,true))");
    run("split.captures","arr(new RegExp(\"(,)\").split(\"a,b,c\"))");
    run("split.nomatch","arr(new RegExp(\",\").split(\"abc\"))");
    run("split.emptytarget","arr(new RegExp(\",\").split(\"\"))");
    run("split.string.method","arr(\",a,,b,\".split(/,/,void,true))");
    run("split.array.method","arr([].split(/,/,\",a,,b,\",void,true))");
    run("split.literal.separator.control","arr(\"a,b,c\".split(\",\"))");
    run("unicode.literal","arr(/猫(犬)/.match(\"前猫犬後\"))");
    run("unicode.casefold","new RegExp(\"Ä\",\"i\").test(\"ä\")");
    run("unicode.supplementary","arr(/./.match(\"😀\"))");
    run("anchor.default","new RegExp(\"^b\").test(\"a\\nb\")");
    run("anchor.m.flag","new RegExp(\"^b\",\"m\").test(\"a\\nb\")");
    run("dot.default","new RegExp(\"a.b\").test(\"a\\nb\")");
    run("dot.s.flag","new RegExp(\"a.b\",\"s\").test(\"a\\nb\")");
    run("lookahead","arr(new RegExp(\"a(?=b)\").match(\"ab\"))");
    run("lookbehind","arr(new RegExp(\"(?<=a)b\").match(\"ab\"))");
    run("backreference","new RegExp(\"(a)\\\\1\").test(\"aa\")");
    run("empty.pattern.match","arr(new RegExp(\"\").match(\"abc\"))");
    run("empty.quantifier","arr(new RegExp(\"a*\").match(\"bbb\"))");
    action("r = new RegExp(\"x\"); r.compile(\"a\",\"i\");");
    run("compile.replaces","r.test(\"A\")");
    run("compile.missing","r.compile()");
    run("test.missing","r.test()");
    run("replace.missing","r.replace(\"a\")");
    run("invalid.bracket","new RegExp(\"[\")");
    run("invalid.paren","new RegExp(\"(\")");
    run("invalid.quantifier","new RegExp(\"*\")");
    run("readonly.index","(r.index = 7)");
    run("setup.errors","__setupError");
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Clear")(&result);
    fn<void(__cdecl*)(VPP)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    fn<void(__cdecl*)(VPP)>("DLL_TJSVariant_Delete")(&result);
    FreeLibrary(module);
}
