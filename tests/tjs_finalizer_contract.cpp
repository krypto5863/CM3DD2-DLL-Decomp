// Public behaviors corroborated by isolated original-DLL cases. Explicitly
// invalidate script objects before teardown; unsafe native shutdown diagnostics
// are NOT passing contracts. Retired dispatch guard checks below are structural
// hardening of our facade, not a claim of original native post-delete safety.
#include "cm3d2/tjs_api.h"
#include "cm3d2/tjs_variant_api.h"
#include "../src/tjs_variant_internal.h"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
unsigned callbacks;
void CM3D2_CALL hook(const CM3D2_TjsParamAccess*, void*) { ++callbacks; }
void expect(CM3D2_TjsScriptHandle& script, CM3D2_TjsVariantHandle& value,
    const char* code, const char* expected) {
    DLL_TJS_EvalScript(&script, code, value);
    char buffer[1024] = {};
    DLL_TJSVariant_AsString(&value, buffer, sizeof(buffer));
    require(std::string(buffer) == expected, code);
}
void liveFinalizers() {
    CM3D2_TjsScriptHandle script = nullptr;
    CM3D2_TjsVariantHandle value = nullptr;
    DLL_TJS_CreateTjsScriptClass(nullptr, &script);
    DLL_TJSVariant_Create(&value);
    DLL_TJS_AddMemberFunction(&script, "lifeNote", hook);
    DLL_TJS_ExecScript(&script,
        "var calls=0; class F { function finalize() { calls++; lifeNote(); } } var obj=new F();", nullptr);
    DLL_TJS_ExecScript(&script, "invalidate obj; invalidate obj;", nullptr);
    expect(script, value, "calls+\":\"+(isvalid obj)", "1:0");
    require(callbacks == 1, "explicit invalidate calls finalizer once");
    DLL_TJS_ExecScript(&script,
        "calls=0; class R { function finalize() { calls++; invalidate this; } } obj=new R(); invalidate obj;", nullptr);
    expect(script, value, "calls+\":\"+(isvalid obj)", "1:0");
    DLL_TJS_ExecScript(&script,
        "calls=0; var caught=\"\"; class Throw { function finalize() { calls++; if(calls==1) throw new Exception(\"final-stop\"); } } obj=new Throw(); try { invalidate obj; } catch(e) { caught=e.message; }", nullptr);
    expect(script, value, "caught+\":\"+calls+\":\"+(isvalid obj)", "final-stop:1:1");
    DLL_TJS_ExecScript(&script, "invalidate obj;", nullptr);
    expect(script, value, "caught+\":\"+calls+\":\"+(isvalid obj)", "final-stop:2:0");
    DLL_TJS_ExecScript(&script,
        "calls=0; obj=%[]; obj.finalize=function() { calls++; }; invalidate obj;", nullptr);
    expect(script, value, "calls+\":\"+(isvalid obj)", "0:0");
    DLL_TJS_ExecScript(&script,
        "obj=[]; obj.finalize=function() { calls++; }; invalidate obj;", nullptr);
    expect(script, value, "calls+\":\"+(isvalid obj)", "0:0");
    DLL_TJSVariant_Delete(&value);
    DLL_TJS_DeleteTjsScriptClass(&script);
    require(script == nullptr, "script cleared after safe teardown");
}
struct RetainedDispatch {
    TJS::iTJSDispatch2* function;
    TJS::iTJSDispatch2* receiver;
    explicit RetainedDispatch(CM3D2_TjsVariantHandle handle) {
        auto& native = static_cast<cm3d2::internal::TjsVariant*>(handle)->native();
        require(native.Type() == TJS::tvtObject, "retained dispatch type");
        function = native.AsObjectNoAddRef(); receiver = native.AsObjectThisNoAddRef();
        require(function != nullptr, "retained dispatch object");
        function->AddRef(); if (receiver) receiver->AddRef();
    }
    ~RetainedDispatch() { function->Release(); if (receiver) receiver->Release(); }
    TJS::tjs_error call() { return function->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, receiver); }
};
void retiredDispatches() {
    CM3D2_TjsScriptHandle script = nullptr;
    CM3D2_TjsVariantHandle value = nullptr;
    DLL_TJS_CreateTjsScriptClass(nullptr, &script);
    DLL_TJSVariant_Create(&value);
    DLL_TJS_AddMemberFunction(&script, "lifeNote", hook);
    DLL_TJS_EvalScript(&script, "lifeNote", value);
    RetainedDispatch host(value);
    const unsigned before = callbacks;
    require(host.call() == TJS_S_OK && callbacks == before+1, "live retained callback");
    DLL_TJS_EvalScript(&script, "Scripts.eval incontextof Scripts", value);
    RetainedDispatch eval(value);
    require(eval.call() == TJS_E_BADPARAMCOUNT, "live Scripts binding");
    DLL_TJSVariant_Delete(&value);
    DLL_TJS_DeleteTjsScriptClass(&script);
    // Churn wrappers so a stale raw address cannot accidentally appear valid.
    for (int i=0; i<12; ++i) {
        DLL_TJS_CreateTjsScriptClass(nullptr, &script);
        require(host.call() == TJS_E_INVALIDOBJECT, "retired callback rejected");
        require(eval.call() == TJS_E_NATIVECLASSCRASH, "retired Scripts owner rejected");
        DLL_TJS_DeleteTjsScriptClass(&script);
    }
    require(callbacks == before+1, "retired callback never invoked");
}
}
int main() {
    try {
        liveFinalizers(); retiredDispatches();
        std::puts("TJS finalizer and retired-dispatch contracts passed"); return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "TJS finalizer: %s\n", e.what()); return 1;
    }
}
