// Exported API oracle for synthetic large-array sort inputs. Each output line
// includes comparator counts and the complete successful post-sort state.
// Thrown sorts report only error/count: native partial state may contain stale
// references, so this probe never joins it. Default/"safe" run isolated child
// processes; "exceptions" isolates throws. argv[2] may also select an exact case.
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
HMODULE module;
void* script;
void* result;
template<class T> T fn(const char* name) {
    auto address = GetProcAddress(module, name);
    if (!address) { std::fprintf(stderr, "missing %s\n", name); std::exit(3); }
    return reinterpret_cast<T>(address);
}
void exec(const char* text) {
    fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_ExecScript")(&script, text, nullptr);
}
void run(const std::string& label, const std::string& expression) {
    const std::string text = "var probeResult; try { probeResult=(" + expression +
        "); } catch(e) { probeResult=\"OUTER:\"+e.message; }";
    exec(text.c_str());
    fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_EvalScript")(&script, "probeResult", result);
    char buffer[65536] = {};
    fn<void(__cdecl*)(void**, char*, int)>("DLL_TJSVariant_AsString")(&result, buffer, sizeof(buffer));
    std::printf("%s type=%d text=", label.c_str(), fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&result));
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(buffer); *p; ++p) {
        if (*p == '\n') std::printf("\\n");
        else if (*p == '\r') std::printf("\\r");
        else if (*p == '\t') std::printf("\\t");
        else std::putchar(*p);
    }
    std::putchar('\n');
}
void isolated(const char* dll, const std::string& label) {
    char self[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string command = "\""+std::string(self)+"\" \""+dll+"\" \""+label+"\"";
    STARTUPINFOA startup = {}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessA(nullptr, &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) {
        std::printf("%s PROCESS-START-ERROR=%lu\n", label.c_str(), GetLastError()); return;
    }
    DWORD exitCode = 0;
    if (WaitForSingleObject(process.hProcess, 2500) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 124); WaitForSingleObject(process.hProcess, 2500);
        std::printf("%s PROCESS-TIMEOUT\n", label.c_str());
    } else {
        GetExitCodeProcess(process.hProcess, &exitCode);
        if (exitCode) std::printf("%s PROCESS-EXIT=%lu\n", label.c_str(), exitCode);
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool supervisor = argc == 2 || (argc == 3 &&
        (std::string(argv[2]) == "safe" || std::string(argv[2]) == "exceptions" ||
            std::string(argv[2]) == "boundaries"));
    if (!supervisor) {
    module = LoadLibraryA(argv[1]);
    if (!module) { std::fprintf(stderr, "LoadLibrary %lu\n", GetLastError()); return 3; }
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
    fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(nullptr, &script);
    exec("var calls=0; var stopAt=0; function asc(a,b) { calls++; if(calls==stopAt) throw new Exception(\"large-stop\"); return a<b; } function desc(a,b) { calls++; return a>b; } function same(a,b) { calls++; return false; } function keyAsc(a,b) { calls++; return int(a/10000)<int(b/10000); } function makeLarge(n,kind) { var a=[]; for(var i=0;i<n;i++) { var v=(i*17+11)%n; if(kind==1) v=\"\"+v; else if(kind==2 && i%2) v=\"\"+v; else if(kind==3) v=((i*17)%7)*10000+i; a.add(v); } return a; } function largeProbe(n,kind,m,stable,stop) { calls=0; stopAt=stop; var a=makeLarge(n,kind); var status=\"OK\"; var rt; try { if(stable===void) rt=a.sort(m); else rt=a.sort(m,stable); status+=\":\"+typeof rt; } catch(e) { return \"ERROR:\"+e.message+\"|calls=\"+calls+\"|count=\"+a.count; } return status+\"|calls=\"+calls+\"|count=\"+a.count+\"|\"+a.join(\",\"); }");
    }
    struct Item { const char* label; const char* value; };
    const int lengths[] = {33,40,41,42,64,65,128,256,408,409,410,411};
    const Item modes[] = {{"default","void"},{"plus","\"+\""},{"minus","\"-\""},
        {"zero","\"0\""},{"nine","\"9\""},{"a","\"a\""},{"z","\"z\""},
        {"ascending","asc"},{"descending","desc"},{"equal","same"},
        {"throw1","asc"},{"throw4","asc"},{"throw25","asc"},{"throw100","asc"}};
    const Item stability[] = {{"default","void"},{"unstable","false"},{"stable","true"}};
    const char* kinds[] = {"numbers","strings","mixed","ties"};
    for (const int n : lengths) for (int kind = 0; kind < 4; ++kind) {
        const bool boundary = n == 42 || n == 408 || n == 409;
        const bool boundarySelection = argc == 3 && std::string(argv[2]) == "boundaries";
        if (supervisor && boundary != boundarySelection) continue;
        for (const auto& mode : modes) for (const auto& stable : stability) {
            if (kind == 3 && std::string(mode.label) != "ascending") continue;
            const std::string label = "n"+std::to_string(n)+"."+kinds[kind]+"."+mode.label+"."+stable.label;
            const bool throwing = std::string(mode.label).find("throw") == 0;
            // Mixed lexical/numeric comparisons can violate strict weak ordering.
            // Restrict mixed inputs to explicitly numeric/string builtin modes.
            if (kind == 2 && std::string(mode.label) != "zero" && std::string(mode.label) != "nine" &&
                std::string(mode.label) != "a" && std::string(mode.label) != "z") continue;
            if (argc == 3 && std::string(argv[2]) == "safe") { if (throwing) continue; }
            else if (boundarySelection) { if (throwing) continue; }
            else if (argc == 3 && std::string(argv[2]) == "exceptions") { if (!throwing) continue; }
            else if (argc == 3 && label != argv[2]) continue;
            else if (argc == 2 && throwing) continue;
            if (supervisor) { isolated(argv[1], label); continue; }
            const std::string cmp = kind == 3 ? "keyAsc" : mode.value;
            int stop = 0;
            if (std::string(mode.label).find("throw") == 0) stop = std::atoi(mode.label+5);
            run(label,"largeProbe("+std::to_string(n)+","+std::to_string(kind)+","+cmp+","+stable.value+","+std::to_string(stop)+")");
        }
    }
    if (supervisor) return 0;
    fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
    fn<void(__cdecl*)(void**)>("DLL_TJS_DeleteTjsScriptClass")(&script);
    FreeLibrary(module);
    return 0;
}
