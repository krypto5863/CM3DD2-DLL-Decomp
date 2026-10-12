// The shipped default TJS engine does not register RegExp. Keep that native
// absence contract instead of enabling a different engine's regex semantics.
#include "cm3d2/tjs_api.h"
#include "cm3d2/tjs_variant_api.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if(!value) throw std::runtime_error(message);
}
void CM3D2_CALL ignoreConsole(const char*, uint8_t) {}
struct Engine {
    CM3D2_TjsScriptHandle script=nullptr;
    CM3D2_TjsVariantHandle result=nullptr;
    Engine() {
        DLL_TJSVariant_Create(&result);
        DLL_TJS_CreateTjsScriptClass(nullptr,&script);
        require(script!=nullptr && result!=nullptr,"create regex contract engine");
        DLL_TJS_CreateTjsConsoleClass(&script,ignoreConsole);
        DLL_TJS_ExecScript(&script,"var regexResult;",nullptr);
    }
    ~Engine() {
        // Even this scalar-only fixture follows the owned-payload cleanup order.
        DLL_TJSVariant_Delete(&result);
        DLL_TJS_DeleteTjsScriptClass(&script);
    }
    std::string caught(const char* expression) {
        std::string code="regexResult = \"NO_ERROR\"; try { regexResult = (";
        code+=expression;
        code+="); } catch(e) { regexResult = e.message; }";
        DLL_TJS_ExecScript(&script,code.c_str(),nullptr);
        DLL_TJS_EvalScript(&script,"regexResult",result);
        require(DLL_TJSVariant_GetType(&result)==2,"caught regex result must be string");
        char text[512]={};
        DLL_TJSVariant_AsString(&result,text,sizeof(text));
        return text;
    }
};
void checkLifecycle() {
    Engine engine;
    const std::string missing="Member \"RegExp\" does not exist";
    require(engine.caught("typeof RegExp")==missing,"native RegExp global absent");
    require(engine.caught("new RegExp(\"a\").test(\"a\")")==missing,
        "constructor must not enable nonnative regex support");
    require(engine.caught("/a/.test(\"a\")")==missing,
        "regex literal resolves absent dynamic RegExp class");
    require(engine.caught("/a/gi.test(\"A\")")==missing,
        "regex literal flags must not bypass absent class");
    require(engine.caught("\"a,b,c\".split(\",\").join(\"|\")")=="a|b|c",
        "ordinary String split remains available without RegExp");
}
}
int main() {
    try {
        checkLifecycle();
        checkLifecycle();
        std::puts("TJS RegExp absence contract passed (two engine lifecycles)");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"TJS RegExp absence contract failed: %s\n",e.what());
        return 1;
    }
}
