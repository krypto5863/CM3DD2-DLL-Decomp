#include "cm3d2/file_system_api.h"
#include "cm3d2/kag_api.h"
#include "cm3d2/tjs_api.h"
#include "cm3d2/tjs_variant_api.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Expected values were taken from the ORIGINAL DLL with tools/differential/kag_exec_probe.cpp, kag_flow_probe.cpp,
// kag_misc_probe.cpp and kag_serialize_probe.cpp (2026-10-11). The rebuilt DLL matches those probes line for line
// except for undefined behaviour of the original (uninitialized parser fields, Exec after a restore).
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

CM3D2_KagHandle g_kag = nullptr;
std::vector<std::string> g_log;

std::string text(CM3D2_KagHandle* handle) {
    char buffer[1024] = {};
    DLL_KAG_GetText(handle, buffer, sizeof(buffer));
    return buffer;
}
std::string label(CM3D2_KagHandle* handle) {
    char buffer[256] = {};
    DLL_KAG_GetCurrentLabel(handle, buffer, sizeof(buffer));
    return buffer;
}
std::string storage(CM3D2_KagHandle* handle) {
    char buffer[256] = {};
    DLL_KAG_GetStoragename(handle, buffer, sizeof(buffer));
    return buffer;
}

void loadString(CM3D2_KagHandle* handle, const char* scenario) {
    DLL_KAG_TextClear(handle);
    DLL_KAG_ClearCallStack(handle);
    DLL_KAG_LoadScenarioString(handle, scenario);
}

void loadFile(CM3D2_KagHandle* handle, const char* name) {
    DLL_KAG_TextClear(handle);
    DLL_KAG_ClearCallStack(handle);
    DLL_KAG_LoadScenario(handle, name);
}

uint8_t CM3D2_CALL attributeTag(CM3D2_KagTagSupport* tag) {
    g_log.push_back("tag valid(text)=" + std::to_string(DLL_KAG_TAG_IsValid(tag, "text")) + " valid(zzz)=" +
        std::to_string(DLL_KAG_TAG_IsValid(tag, "zzz")));
    DLL_KAG_TAG_CreateTagList(tag);
    const int count = static_cast<int>(DLL_KAG_TAG_GetListCount(tag));
    for (int i = 0; i < count; ++i) {
        char name[64] = {};
        char value[64] = {};
        DLL_KAG_TAG_GetListNameAt(tag, i, name, sizeof(name));
        DLL_KAG_TAG_GetListElmAt(tag, i, value, sizeof(value));
        g_log.push_back(std::string(name) + "=" + value);
    }
    return 0;
}

void CM3D2_CALL onCall(const CM3D2_KagScript*, const char* a, const char* b) { g_log.push_back(std::string("call ") + a + "|" + b); }
void CM3D2_CALL onRCall(const CM3D2_KagScript*, const char* a, const char* b) { g_log.push_back(std::string("rcall ") + a + "|" + b); }
void CM3D2_CALL onLoad(const CM3D2_KagScript*, const char* a, const char* b) { g_log.push_back(std::string("load ") + a + "|" + b); }
void CM3D2_CALL onReturn(const CM3D2_KagScript*, const char* a, int n) { g_log.push_back(std::string("return ") + a + "|" + std::to_string(n)); }
void CM3D2_CALL onReplace(const CM3D2_KagScript*, const char* in, char* out, int capacity) {
    g_log.push_back(std::string("replace ") + in);
    std::snprintf(out, static_cast<size_t>(capacity), "%s", std::strcmp(in, "swap.ks") == 0 ? "b.ks" : in);
}
void CM3D2_CALL onLabel(CM3D2_KagScript*, uint8_t seen) { g_log.push_back("label seen=" + std::to_string(seen)); }

void write(const std::filesystem::path& folder, const char* name, const char* content) {
    std::ofstream(folder / name, std::ios::binary) << content;
}

} // namespace

int main() try {
    const auto folder = std::filesystem::temp_directory_path() / "cm3d2_kag_script_contract";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    write(folder, "a.ks", "*start\nA1[call file=\"b.ks\" label=\"*sub\"]\nA2[jump label=\"*end\"]\nA3\n*end\nA4[s]\nA5\n");
    write(folder, "b.ks", "*sub\nB1[return]\nB2\n");
    write(folder, "d.ks", "*d0\nD1[r_call file=\"b\"]\nD2[r_return]\nD3\n");
    write(folder, "rr.ks", "[r_call file=\"b.ks\"]\n");
    write(folder, "f.ks", "*f0\nF1[call file=\"swap.ks\" label=\"*sub\"]\nF2\n");

    CM3D2_TjsScriptHandle tjs = nullptr;
    DLL_TJS_CreateTjsScriptClass(nullptr, &tjs);
    CM3D2_FileSystemHandle fs{};
    DLL_FileSystem_CreateFileSystemWindows(&fs);
    require(DLL_FileSystem_AddFolder(&fs, folder.string().c_str()) == 1, "add folder");
    void* wide = DLL_FileSystem_GetInterfaceFileSystemWidePointer(&fs);
    DLL_KAG_Create(&tjs, &g_kag);
    require(g_kag != nullptr, "created");
    DLL_KAG_SetFileSystem(&g_kag, wide);
    require(DLL_KAG_GetUniqueID(&g_kag) == 0, "first unique id");
    require(DLL_KAG_GetTagCallEnabled(&g_kag) == 1 && DLL_KAG_GetTagReturnEnabled(&g_kag) == 1, "tag flags default on");

    // Text, [r] adds nothing, labels, line numbers.
    loadString(&g_kag, "*start\nHello[r]\nWorld\n");
    require(DLL_KAG_IsExecScenario(&g_kag) == 1 && storage(&g_kag) == "string scenario : no0", "string scenario name");
    require(DLL_KAG_Exec(&g_kag) == 0 && DLL_KAG_IsExecScenario(&g_kag) == 0, "plain exec ends");
    require(text(&g_kag) == "HelloWorld" && label(&g_kag) == "*start" && DLL_KAG_GetCurrentLine(&g_kag) == 3, "plain text state");

    // Custom tag: attributes come in the engine's dictionary order.
    DLL_KAG_AddTag(&g_kag, "mytag", attributeTag);
    g_log.clear();
    loadString(&g_kag, "A[mytag text=\"x\" stop=1 n=5]B\nC\n");
    require(DLL_KAG_Exec(&g_kag) == 0 && text(&g_kag) == "ABC", "custom tag does not stop");
    require(g_log.size() == 5 && g_log[0] == "tag valid(text)=1 valid(zzz)=0" && g_log[1] == "n=5" && g_log[2] == "stop=1" &&
        g_log[3] == "text=x" && g_log[4] == "tagname=mytag", "tag attribute order");

    // [eval] failure stops Exec (returns 1) and leaves the script running; the next Exec continues.
    loadString(&g_kag, "[eval exp=\"f.v=3\"]\n[macro name=\"m1\"]M[endmacro]\n[m1]X\n");
    require(DLL_KAG_Exec(&g_kag) == 1 && DLL_KAG_IsExecScenario(&g_kag) == 1 && DLL_KAG_GetCurrentLine(&g_kag) == 0, "eval failure stops");
    require(DLL_KAG_Exec(&g_kag) == 0 && text(&g_kag) == "MX" && DLL_KAG_GetCurrentLine(&g_kag) == 3, "macro expansion");

    // Unknown tags are skipped; [s] pauses until a load or jump.
    loadString(&g_kag, "[nosuchtag a=1]\nafter\n[s]\nlater\n");
    require(DLL_KAG_Exec(&g_kag) == 1 && text(&g_kag) == "after", "unknown tag skipped, [s] stops");
    require(DLL_KAG_Exec(&g_kag) == 1 && text(&g_kag) == "after", "paused script keeps returning 1");
    DLL_KAG_GoToLabel(&g_kag, "*nothing");  // label missing: error is reported, state unchanged
    require(DLL_KAG_Exec(&g_kag) == 1, "still paused after a failed jump");

    // call / jump / return with files and callbacks.
    DLL_KAG_SetOnCallFunction(&g_kag, onCall);
    DLL_KAG_SetOnScenarioLoadFunction(&g_kag, onLoad);
    DLL_KAG_SetOnReturnFunction(&g_kag, onReturn);
    DLL_KAG_SetOnLabelCallBackFunction(&g_kag, onLabel);
    g_log.clear();
    loadFile(&g_kag, "a.ks");
    require(DLL_KAG_Exec(&g_kag) == 1, "[s] in a.ks stops");
    require(text(&g_kag) == "A1B1A2A4" && label(&g_kag) == "*end" && DLL_KAG_GetCurrentLine(&g_kag) == 5, "a.ks text and position");
    require(DLL_KAG_GetCallStackDepth(&g_kag) == 0, "call stack balanced after return");
    bool saw_call = false, saw_return = false, saw_label = false;
    for (const auto& entry : g_log) {
        saw_call = saw_call || entry == "call b.ks|*sub";
        saw_return = saw_return || entry == "return a.ks|1";
        saw_label = saw_label || entry == "label seen=0";
    }
    require(saw_call && saw_return && saw_label, "call, return and label callbacks fired");
    DLL_KAG_SetOnCallFunction(&g_kag, nullptr);
    DLL_KAG_SetOnScenarioLoadFunction(&g_kag, nullptr);
    DLL_KAG_SetOnReturnFunction(&g_kag, nullptr);
    DLL_KAG_SetOnLabelCallBackFunction(&g_kag, nullptr);

    // Replace callback sees the call target before the load.
    DLL_KAG_SetReplaceFunction(&g_kag, onReplace);
    DLL_KAG_SetOnCallFunction(&g_kag, onCall);
    g_log.clear();
    loadFile(&g_kag, "f.ks");
    require(DLL_KAG_Exec(&g_kag) == 0 && text(&g_kag) == "F1B1F2" && storage(&g_kag) == "f.ks", "replace swaps the storage");
    require(g_log.size() >= 2 && g_log[0] == "replace swap.ks" && g_log[1] == "call b.ks|*sub", "replace and call callbacks");
    DLL_KAG_SetReplaceFunction(&g_kag, nullptr);
    DLL_KAG_SetOnCallFunction(&g_kag, nullptr);

    // r_call: file name rule found against the original.
    DLL_KAG_SetOnRCallFunction(&g_kag, onRCall);
    g_log.clear();
    loadFile(&g_kag, "d.ks");
    DLL_KAG_Exec(&g_kag);
    require(!g_log.empty() && g_log[0] == "rcall b.ks|", "r_call appends .ks to a bare name");
    require(DLL_KAG_GetRCallStackDepth(&g_kag) == 1, "r_call pushed a record");
    g_log.clear();
    loadFile(&g_kag, "rr.ks");
    DLL_KAG_Exec(&g_kag);
    require(!g_log.empty() && g_log[0] == "rcall ksks|", "a dotted name becomes the extension twice");
    DLL_KAG_SetOnRCallFunction(&g_kag, nullptr);

    // AddTag keeps the first handler; RemoveTag reports whether it removed one.
    require(DLL_KAG_RemoveTag(&g_kag, "mytag") == 1 && DLL_KAG_RemoveTag(&g_kag, "mytag") == 0, "remove tag");
    loadString(&g_kag, "A[mytag]B\n");
    require(DLL_KAG_Exec(&g_kag) == 0 && text(&g_kag) == "AB", "removed tag is unknown");
    DLL_KAG_RemoveTag(&g_kag, "ch");
    loadString(&g_kag, "A[ch text=\"X\"]B\n");
    DLL_KAG_Exec(&g_kag);
    require(text(&g_kag) == "", "removing ch: nothing is appended");  // the original also drops the A and B text runs

    // Tag enable flags: a disabled call is skipped like an unknown tag.
    CM3D2_KagHandle second = nullptr;
    DLL_KAG_Create(&tjs, &second);
    DLL_KAG_SetFileSystem(&second, wide);
    require(DLL_KAG_GetUniqueID(&second) == 1 && DLL_KAG_GetUniqueIDFromBasicKagScriptPtr(static_cast<const CM3D2_KagScript*>(second)) == 1, "second unique id");
    DLL_KAG_SetTagCallEnabled(&second, 0);
    loadString(&second, "A[call file=\"b.ks\" label=\"*sub\"]C\n");
    DLL_KAG_Exec(&second);
    require(text(&second) == "AC" && DLL_KAG_GetCallStackDepth(&second) == 0, "call disabled");
    DLL_KAG_SetTagCallEnabled(&second, 1);
    DLL_KAG_SetTagReturnEnabled(&second, 0);
    loadString(&second, "A[call file=\"b.ks\" label=\"*sub\"]C\n");
    DLL_KAG_Exec(&second);
    require(text(&second) == "AB1B2" && DLL_KAG_GetCallStackDepth(&second) == 1, "return disabled");
    DLL_KAG_Delete(&second);
    require(second == nullptr, "deleted");

    // State text of a fresh parser and of a string scenario: the original's struct string.
    CM3D2_KagHandle third = nullptr;
    DLL_KAG_Create(&tjs, &third);
    CM3D2_KagSerializeData* data = DLL_KAG_CreateSerializeData(&third);
    if (DLL_KAG_GetSerializeDataSize(data) != 673) std::fprintf(stderr, "fresh serialize size %d\n", DLL_KAG_GetSerializeDataSize(data));
    require(DLL_KAG_GetSerializeDataSize(data) == 673, "fresh serialize size (original: 673 bytes)");
    DLL_KAG_ReleaseSerializeData(data);
    DLL_KAG_Delete(&third);

    DLL_KAG_Delete(&g_kag);
    DLL_TJS_DeleteTjsScriptClass(&tjs);
    std::filesystem::remove_all(folder);
    std::puts("kag_script_contract ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAILED: %s\n", error.what());
    return 1;
}
