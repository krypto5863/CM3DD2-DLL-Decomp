#include "cm3d2/tjs_api.h"
#include "cm3d2/tjs_variant_api.h"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
void CM3D2_CALL console(const char*, uint8_t) {}
struct Engine {
    CM3D2_TjsScriptHandle script = nullptr;
    CM3D2_TjsVariantHandle result = nullptr;
    Engine() {
        DLL_TJSVariant_Create(&result);
        DLL_TJS_CreateTjsScriptClass(nullptr, &script);
        DLL_TJS_CreateTjsConsoleClass(&script, console);
        exec("var output; var calls; function asc(a,b) { calls++; return a<b; } function same(a,b) { calls++; return false; } function probe(a,m,stable) { calls=0; var state=\"OK\"; try { a.sort(m,stable); } catch(e) { state=e.message; } return state+\"|\"+calls+\"|\"+a.join(\",\"); }");
    }
    ~Engine() { DLL_TJSVariant_Delete(&result); DLL_TJS_DeleteTjsScriptClass(&script); }
    void exec(const char* source) { DLL_TJS_ExecScript(&script, source, nullptr); }
    std::string eval(const char* source) {
        DLL_TJSVariant_Clear(&result);
        DLL_TJS_EvalScript(&script, source, result);
        char text[8192]{};
        DLL_TJSVariant_AsString(&result, text, sizeof(text));
        return text;
    }
};
}
int main() {
    try {
        Engine engine;
        require(engine.eval("probe([],void,false)") == "OK|0|", "empty sort performs no comparisons");
        require(engine.eval("probe([3],void,true)") == "OK|0|3", "singleton stable sort performs no comparisons");
        require(engine.eval("probe([3,1],void,false)") == "Cannot convert the variable type ((int)1 to Object)|0|3,1", "native default comparator failure");
        require(engine.eval("probe([1,3],\"+\",true)") == "Cannot convert the variable type ((int)0 to Object)|0|1,3", "comparison evaluated before conversion failure");
        require(engine.eval("probe([3,1,2,1,10],\"a\",false)") == "OK|0|1,1,10,2,3", "native mixed-type string comparison succeeds");
        require(engine.eval("probe([\"z\",\"a\",\"middle\"],\"a\",true)") == "Cannot convert the variable type ((int)1 to Object)|0|z,a,middle", "native two-string comparator failure");
        require(engine.eval("probe([3,\"10\",2,\"1\"],\"a\",false)") == "Cannot convert the variable type ((int)1 to Object)|0|10,2,3,1", "native partial state at failed string comparison");
        require(engine.eval("probe([3,1],asc,false)") == "OK|1|1,3", "no Debug-only extra callback comparisons");
        require(engine.eval("probe([3,1,2,1,10],asc,true)") == "OK|10|1,1,2,3,10", "native stable insertion comparison count");
        require(engine.eval("probe([3,1,2,1,10],same,false)") == "OK|8|3,1,2,1,10", "equal values preserve native order and callback count");
        engine.exec("var d=%[\"z\"=>3,\"a\"=>1,\"middle\"=>2]; var keys=(Dictionary.getKeyArray incontextof d)();");
        require(engine.eval("probe(keys,asc,false)") == "OK|3|a,middle,z", "dictionary key arrays sort with callback");
        engine.exec("function bad4(a,b) { calls++; if(calls==4) throw new Exception(\"stop-after-four\"); return a<b; }");
        require(engine.eval("probe([3,1,2,1,10],bad4,true)") == "stop-after-four|4|1,3,3,1,10", "native partial copy state after callback exception");
        engine.exec("var boundary=[]; for(var n=32; n>0; n--) boundary.push(n); output=probe(boundary,asc,true);");
        require(engine.eval("boundary[0]+\":\"+boundary[31]+\":\"+calls") == "1:32:31", "native 32-element insertion boundary");
        engine.exec("var returned=[3,1]; var r=returned.sort(asc); output=typeof r;");
        require(engine.eval("output") == "void", "sort returns void");
        engine.exec("function large(n) { var a=[]; for(var i=0;i<n;i++) a.add((i*17+11)%n); return a; }");
        const struct { int n, unstable, stable; } cases[] = {
            {33,174,173}, {64,544,633}, {128,1296,1344}, {411,5565,4522}
        };
        for(const auto& item : cases) for(bool stable : {false,true}) {
            const auto n=std::to_string(item.n);
            engine.exec(("var data=large("+n+"); output=probe(data,asc,"+(stable?"true":"false")+");").c_str());
            const auto expected="0:"+std::to_string(item.n-1)+":"+
                std::to_string(stable?item.stable:item.unstable);
            require(engine.eval(("data[0]+\":\"+data["+std::to_string(item.n-1)+"]+\":\"+calls").c_str())==expected,
                "native large sort endpoints and comparator count");
        }
        std::puts("TJS native array sort contracts passed");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"TJS array sort contract failed: %s\n",error.what());
        return 1;
    }
}
