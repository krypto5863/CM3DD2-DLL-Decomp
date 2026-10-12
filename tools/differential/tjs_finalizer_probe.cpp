// Synthetic exported-API lifetime oracle. Each case owns a fresh child process;
// no game inputs, storage calls, or borrowed-variant deletion. Crash witnesses
// are diagnostic only and are not part of the passing parity corpus.
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>

namespace {
HMODULE module;
void* script;
void* value;
template<class T> T fn(const char* name) {
    auto p = GetProcAddress(module, name);
    if (!p) { std::fprintf(stderr, "missing %s\n", name); std::exit(3); }
    return reinterpret_cast<T>(p);
}
void exec(const char* code) {
    fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_ExecScript")(&script, code, nullptr);
}
void eval(const char* code) {
    fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_EvalScript")(&script, code, value);
    char text[4096] = {};
    fn<void(__cdecl*)(void**, char*, int)>("DLL_TJSVariant_AsString")(&value, text, sizeof(text));
    std::printf("eval %s => %s\n", code, text);
}
void __cdecl console(const char* message, std::uint8_t exception) {
    // Diagnostics differ between engine versions. Keep just their occurrence;
    // script-caught error text is compared explicitly by the cases below.
    std::printf("console exception=%u\n", unsigned(exception));
    (void)message;
}
void __cdecl note(const void* params, void*) {
    const int count = fn<int(__cdecl*)(const void*)>("DLL_TJS_ParamAccess_Size")(params);
    std::printf("finalize callback count=%d", count);
    if (count) {
        void* raw = fn<void*(__cdecl*)(const void*, int)>("DLL_TJS_ParamAccess_At")(params, 0);
        void* borrowed = nullptr;
        fn<void(__cdecl*)(void**, void*)>("DLL_TJSVariant_Reference")(&borrowed, raw);
        char text[256] = {};
        fn<void(__cdecl*)(void**, char*, int)>("DLL_TJSVariant_AsString")(&borrowed, text, sizeof(text));
        std::printf(" text=%s", text);
    }
    std::putchar('\n');
}
LONG WINAPI crash(EXCEPTION_POINTERS* info) {
    const auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    std::printf("crash code=%08lx", info->ExceptionRecord->ExceptionCode);
    HMODULE owner = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const char*>(address), &owner) && owner == module)
        std::printf(" dll-rva=%llx", static_cast<unsigned long long>(address-base));
    else std::printf(" location=outside-target-dll");
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        std::printf(" access=%llu target=%llx",
            static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[0]),
            static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
    std::putchar('\n');
    return EXCEPTION_EXECUTE_HANDLER;
}
void isolated(const char* dll, const char* label) {
    char self[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string command = "\""+std::string(self)+"\" \""+dll+"\" "+label;
    STARTUPINFOA startup = {}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessA(nullptr, &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) {
        std::printf("%s START-ERROR=%lu\n", label, GetLastError()); return;
    }
    if (WaitForSingleObject(process.hProcess, 4000) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 2500);
        std::printf("%s TIMEOUT\n", label);
    } else {
        DWORD status = 0;
        GetExitCodeProcess(process.hProcess, &status);
        std::printf("%s EXIT=%lu\n", label, status);
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
struct Case { const char* label; const char* setup; const char* action; const char* check; };
const Case cases[] = {
    {"empty", "", "", "1"},
    {"empty-finalize-drop", "class F { function finalize() {} } var obj=new F();", "obj=void;", "1"},
    {"empty-finalize-shutdown", "class F { function finalize() {} } var obj=new F();", "", "1"},
    {"counter-drop", "var calls=0; class F { function finalize() { calls++; } } var obj=new F();", "obj=void;", "calls"},
    {"counter-invalidate", "var calls=0; class F { function finalize() { calls++; } } var obj=new F();", "invalidate obj; invalidate obj;", "calls+\":\"+(isvalid obj)"},
    {"host-drop", "class F { function finalize() { lifeNote(\"drop\"); } } var obj=new F();", "obj=void;", "1"},
    {"host-invalidate", "class F { function finalize() { lifeNote(\"invalidate\"); } } var obj=new F();", "invalidate obj;", "isvalid obj"},
    {"host-shutdown", "class F { function finalize() { lifeNote(\"shutdown\"); } } var obj=new F();", "", "1"},
    {"counter-shutdown", "var calls=0; class F { function finalize() { calls++; } } var obj=new F();", "", "calls"},
    {"reentrant", "var calls=0; class F { function finalize() { calls++; invalidate this; } } var obj=new F();", "invalidate obj;", "calls+\":\"+(isvalid obj)"},
    {"throw-invalidate", "var calls=0; var caught=\"\"; class F { function finalize() { calls++; throw new Exception(\"final-stop\"); } } var obj=new F();", "try { invalidate obj; } catch(e) { caught=e.message; }", "caught+\":\"+calls+\":\"+(isvalid obj)"},
    {"throw-retry", "var calls=0; var caught=\"\"; class F { function finalize() { calls++; if(calls==1) throw new Exception(\"final-stop\"); } } var obj=new F();", "try { invalidate obj; } catch(e) { caught=e.message; } invalidate obj;", "caught+\":\"+calls+\":\"+(isvalid obj)"},
    {"dictionary-finalize", "var calls=0; var obj=%[]; obj.finalize=function() { calls++; };", "invalidate obj;", "calls+\":\"+(isvalid obj)"},
    {"array-finalize", "var calls=0; var obj=[]; obj.finalize=function() { calls++; };", "invalidate obj;", "calls+\":\"+(isvalid obj)"},
    {"member-drop", "var calls=0; class F { function finalize() { calls++; } } var holder=%[\"child\"=>new F()];", "delete holder.child;", "calls"},
    {"local-drop", "var calls=0; class F { function finalize() { calls++; } } function local() { var obj=new F(); }", "local();", "calls"},
    {"invalid-member", "class F { function finalize() {} } var obj=new F(); var caught=\"\";", "invalidate obj; try { obj.x=2; } catch(e) { caught=e.message; }", "caught"},
    {"retained-clear", "var calls=0; class F { function finalize() { calls++; lifeNote(\"owned\"); } } var obj=new F();", "", "calls"},
    {"two-engines", "var marker=23;", "", "marker"},
    {"instance-local-shutdown", "class F { var marker=0; function finalize() { marker++; } } var obj=new F();", "", "obj.marker"},
    {"instance-host-invalidate", "class F { var hook; function F() { hook=lifeNote; } function finalize() { hook(\"instance\"); } } var obj=new F();", "invalidate obj;", "isvalid obj"},
    {"instance-host-shutdown", "class F { var hook; function F() { hook=lifeNote; } function finalize() { hook(\"instance\"); } } var obj=new F();", "", "1"},
};
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(crash);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool safe = argc == 3 && std::string(argv[2]) == "safe";
    const bool diagnostics = argc == 3 && std::string(argv[2]) == "diagnostics";
    if (argc == 2 || safe || diagnostics) {
        for (const auto& c : cases) {
            const std::string label = c.label;
            const bool unsafe = label == "host-drop" || label == "host-shutdown" ||
                label == "counter-shutdown" || label == "retained-clear" || label == "instance-host-shutdown";
            if ((safe && unsafe) || (diagnostics && !unsafe)) continue;
            isolated(argv[1], c.label);
        }
        return 0;
    }
    const Case* selected = nullptr;
    for (const auto& c : cases) if (std::string(argv[2]) == c.label) selected = &c;
    if (!selected) return 2;
    module = LoadLibraryA(argv[1]);
    if (!module) return 3;
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&value);
    fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &script);
    fn<void(__cdecl*)(void**, void(__cdecl*)(const char*,std::uint8_t))>("DLL_TJS_CreateTjsConsoleClass")(&script,console);
    fn<void(__cdecl*)(void**,const char*,void(__cdecl*)(const void*,void*))>("DLL_TJS_AddMemberFunction")(&script,"lifeNote",note);
    std::printf("case %s setup\n", selected->label);
    exec(selected->setup);
    if (std::string(selected->label) == "retained-clear") {
        fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_EvalScript")(&script, "obj", value);
        exec("obj=void;");
        std::puts("stage owned-clear");
        fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Clear")(&value);
    }
    exec(selected->action); eval(selected->check);
    // Throwing finalizers are retried explicitly before leaving to avoid an
    // unhandled exception in shutdown obscuring the explicit-invalidation test.
    if (std::string(selected->label) == "throw-invalidate") exec("obj.finalize=function() {}; invalidate obj;");
    if (std::string(selected->label) == "two-engines") {
        void* other = nullptr;
        fn<void(__cdecl*)(void*,void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr,&other);
        fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&other);
        eval("marker+1");
    }
    std::puts("stage variant-delete");
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&value);
    std::puts("stage engine-delete");
    fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    std::printf("stage deleted=%d\n", script==nullptr);
    FreeLibrary(module);
    return 0;
}
