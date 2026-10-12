#include "cm3d2/tjs_api.h"
#include "cm3d2/tjs_variant_api.h"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
void CM3D2_CALL console(const char*, uint8_t) {}
struct Engine {
    CM3D2_TjsScriptHandle script = nullptr;
    CM3D2_TjsVariantHandle result = nullptr;
    Engine() {
        DLL_TJSVariant_Create(&result);
        DLL_TJS_CreateTjsScriptClass(nullptr, &script);
        DLL_TJS_CreateTjsConsoleClass(&script, console);
        exec("var output; function struct(d) { return (Dictionary.toStructString incontextof d)(); } function keys(d) { return (Dictionary.getKeyArray incontextof d)(); }");
    }
    ~Engine() { DLL_TJSVariant_Delete(&result); DLL_TJS_DeleteTjsScriptClass(&script); }
    void exec(const char* source) { DLL_TJS_ExecScript(&script, source, nullptr); }
    std::string eval(const char* source) {
        std::string guarded="output = \"UNASSIGNED\"; try { output = (";
        guarded += source;
        guarded += "); } catch(e) { output = \"ERROR:\" + e.message; }";
        exec(guarded.c_str());
        DLL_TJS_EvalScript(&script, "output", result);
        char text[8192]{};
        DLL_TJSVariant_AsString(&result, text, sizeof(text));
        return text;
    }
};
}
int main() {
    try {
        Engine engine;
        require(engine.eval("typeof Dictionary.toStructString") == "Object", "class struct method registered");
        require(engine.eval("typeof Dictionary.getKeyArray") == "Object", "class key method registered");
        require(engine.eval("typeof (%[]).toStructString") == "undefined", "custom methods are class-only");
        require(engine.eval("typeof Dictionary.loadStruct") == "undefined", "no newer vendor dictionary loadStruct");
        require(engine.eval("typeof ([]).loadStruct") == "undefined", "no newer vendor array loadStruct");
        require(engine.eval("struct(%[])") == "(const)%[]", "empty compact struct");
        require(engine.eval("struct(%[\"a\"=>1])") == "(const)%[\"a\",1]", "compact comma key/value format");
        require(engine.eval("struct(%[\"real\"=>1.25])") == "(const)%[\"real\",0x1.4000000000000p0]", "native hex real without vendor comment");
        require(engine.eval("struct(%[\"octet\"=><% 00 7f 80 ff %>])") == "(const)%[\"octet\",<%00 7f 80 ff %>]", "native octet spacing");
        require(engine.eval("struct(%[\"a\"=>[1,\"two\",void]])") == "(const)%[\"a\",(const)[1,\"two\",void]]", "nested compact array");
        require(engine.eval("struct(%[\"a\"=>%[\"x\"=>7]])") == "(const)%[\"a\",(const)%[\"x\",7]]", "nested compact dictionary");
        require(engine.eval("struct(%[\"s\"=>\"quote\\\" slash\\\\ newline\\n\"])") == "(const)%[\"s\",\"quote\\\" slash\\\\ newline\n\"]", "only quote and backslash escaped");
        require(engine.eval("keys(%[\"z\"=>3,\"a\"=>1,\"middle\"=>2]).join(\"|\")") == "middle|a|z", "native dictionary enumeration order");
        engine.exec("var one=%[\"a\"=>1]; var bound=Dictionary.toStructString incontextof one;");
        require(engine.eval("bound(1,null)") == "(const)%[\"a\",1]", "arguments ignored with bound receiver");
        require(engine.eval("Dictionary.toStructString()") == "ERROR:Invalid object context", "unbound class context rejected");
        require(engine.eval("Dictionary.getKeyArray()") == "ERROR:Invalid object context", "unbound key context rejected");
        require(engine.eval("struct(Scripts.eval(struct(one)))") == "(const)%[\"a\",1]", "struct expression round trip");
        // Hardening only: native dereferences null and recurses unchecked.
        require(engine.eval("struct(%[\"a\"=>null])") == "(const)%[\"a\",null]", "null object safety normalization");
        engine.exec("var cycle=%[]; cycle.self=cycle;");
        require(engine.eval("struct(cycle)").find("ERROR:Unsupported cyclic") == 0, "cycle safety normalization");
        engine.exec("delete cycle.self;");
        std::puts("TJS native Dictionary contracts passed");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"TJS Dictionary contract failed: %s\n",error.what());
        return 1;
    }
}
