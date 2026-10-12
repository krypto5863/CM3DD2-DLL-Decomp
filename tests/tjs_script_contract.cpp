#include "cm3d2/tjs_api.h"
#include "cm3d2/tjs_variant_api.h"
#include "cm3d2/file_system_delegate_api.h"
#include "../src/file_interface.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
unsigned callback_count = 0;
unsigned errors = 0;
unsigned null_results = 0;
std::string last_error;
std::uint64_t callback_id = 0;
std::uint64_t callback_key = 0;
unsigned storage_files_alive = 0;
unsigned storage_files_destroyed = 0;
std::wstring requested_path;
CM3D2_TjsScriptHandle* payload_script = nullptr;
const char* payload_source = nullptr;
unsigned payload_calls = 0;
unsigned argument_mutations = 0;

void CM3D2_CALL payload(const CM3D2_TjsParamAccess* parameters, void* output) {
    ++payload_calls;
    require(DLL_TJS_ParamAccess_Size(parameters) == 2, "payload argument count");
    CM3D2_TjsVariantHandle object = nullptr, octet = nullptr;
    DLL_TJSVariant_Reference(&object, DLL_TJS_ParamAccess_At(parameters, 0));
    DLL_TJSVariant_Reference(&octet, DLL_TJS_ParamAccess_At(parameters, 1));
    require(DLL_TJSVariant_GetType(&object) == 1 && DLL_TJSVariant_GetType(&octet) == 3,
        "object/octet callback tags");
    char text[128]{};
    DLL_TJSVariant_AsString(&object, text, sizeof(text));
    require(std::string(text).find("(object 0x") == 0, "object callback string conversion");
    if (output != nullptr) DLL_TJS_EvalScript(payload_script, payload_source, output);
}

void CM3D2_CALL mutatePayload(const CM3D2_TjsParamAccess* parameters, void*) {
    CM3D2_TjsVariantHandle argument = nullptr, alias = nullptr;
    const auto raw = DLL_TJS_ParamAccess_At(parameters, 0);
    DLL_TJSVariant_Reference(&argument, raw);
    DLL_TJSVariant_Reference(&alias, raw);
    DLL_TJSVariant_SetString(&argument, "changed");
    require(DLL_TJSVariant_GetType(&alias) == 2, "setter is immediately visible through alias");
    DLL_TJSVariant_Clear(&argument);
    require(DLL_TJSVariant_GetType(&alias) == 0, "clear is immediately visible through alias");
    ++argument_mutations;
}

class TrackedScriptFile final : public cm3d2::internal::IFile {
public:
    explicit TrackedScriptFile(const std::vector<unsigned char>& bytes) {
        DLL_FileMemory_CreateileMemory(&file_);
        DLL_FileMemory_SetData(&file_, bytes.data(), static_cast<int32_t>(bytes.size()));
        ++storage_files_alive;
    }
    ~TrackedScriptFile() override {
        DLL_FileMemory_DeleteileMemory(&file_);
        --storage_files_alive;
        ++storage_files_destroyed;
    }
    void close() override { underlying()->close(); }
    bool seek(std::int64_t offset, bool absolute) override { return underlying()->seek(offset, absolute); }
    std::uint64_t read(void* out, std::uint64_t size) override { return underlying()->read(out, size); }
    std::uint64_t readAt(void* out, std::uint64_t offset, std::uint64_t size) override {
        return underlying()->readAt(out, offset, size);
    }
    bool valid() const override { return underlying()->valid(); }
    const std::byte* data() const override { return underlying()->data(); }
    std::uint64_t tell() const override { return underlying()->tell(); }
    std::uint64_t size() const override { return underlying()->size(); }
private:
    cm3d2::internal::IFile* underlying() const { return static_cast<cm3d2::internal::IFile*>(file_); }
    CM3D2_FileHandle file_ = nullptr;
};

CM3D2_FileHandle CM3D2_CALL scriptFile(const wchar_t* name) {
    requested_path = name;
    if (requested_path == L"eval.tjs") return new TrackedScriptFile({'4','0','+','2'});
    if (requested_path == L"exec.tjs") {
        const std::string source = "var storageSaved = 73;";
        return new TrackedScriptFile(std::vector<unsigned char>(source.begin(), source.end()));
    }
    if (requested_path == L"empty.tjs") return new TrackedScriptFile({});
    if (requested_path == L"bad.tjs") return new TrackedScriptFile({0xff,0xfe,0,0,'4',0,0,0});
    if (requested_path == L"syntax.tjs") return new TrackedScriptFile({'1',' ','+'});
    return nullptr;
}

void CM3D2_CALL console(const char* text, uint8_t exception) {
    if (exception) { ++errors; last_error = text; }
}

void CM3D2_CALL sum(const CM3D2_TjsParamAccess* parameters, void* output) {
    ++callback_count;
    callback_id = DLL_TJS_ParamAccess_GetClassUniqueId(parameters);
    callback_key = DLL_TJS_ParamAccess_GetNameKey(parameters);
    int total = 0;
    for (int i = 0; i < DLL_TJS_ParamAccess_Size(parameters); ++i) {
        CM3D2_TjsVariantHandle argument = nullptr;
        DLL_TJSVariant_Reference(&argument, DLL_TJS_ParamAccess_At(parameters, i));
        total += static_cast<int>(DLL_TJSVariant_AsInteger(&argument));
        // These arguments are borrowed; never Delete a referenced callback value.
    }
    if (output != nullptr) {
        CM3D2_TjsVariantHandle result = nullptr;
        DLL_TJSVariant_Reference(&result, output);
        DLL_TJSVariant_SetInteger(&result, total);
    } else ++null_results;
}

struct Fixture {
    CM3D2_TjsScriptHandle script = nullptr;
    CM3D2_TjsVariantHandle result = nullptr;
    explicit Fixture(void* filesystem = nullptr) {
        DLL_TJS_CreateTjsScriptClass(filesystem, &script);
        DLL_TJSVariant_Create(&result);
        require(script != nullptr && result != nullptr, "created script and variant");
    }
    ~Fixture() {
        DLL_TJSVariant_Delete(&result);
        DLL_TJS_DeleteTjsScriptClass(&script);
    }
    void eval(const char* text) { DLL_TJS_EvalScript(&script, text, result); }
    std::string text() {
        char bytes[256]{};
        DLL_TJSVariant_AsString(&result, bytes, sizeof(bytes));
        return bytes;
    }
    void file(const std::vector<unsigned char>& bytes, bool evaluate) {
        CM3D2_FileHandle file = nullptr;
        DLL_FileMemory_CreateileMemory(&file);
        DLL_FileMemory_SetData(&file, bytes.data(), static_cast<int32_t>(bytes.size()));
        void* interface_file = DLL_FileMemory_GetInterfaceFilePointer(&file);
        if (evaluate) DLL_TJS_EvalScriptFile(&script, interface_file, result);
        else DLL_TJS_ExecScriptFile(&script, interface_file, result);
        require(DLL_File_IsValid(&file) != 0, "script borrows file");
        DLL_FileMemory_DeleteileMemory(&file);
    }
};
} // namespace

int main() try {
    Fixture first;
    const auto first_id = DLL_TJS_GetUniqueID(&first.script);
    Fixture second;
    require(DLL_TJS_GetUniqueID(&second.script) == first_id + 1, "unique IDs increase");
    first.eval("6 * 7");
    require(DLL_TJSVariant_GetType(&first.result) == 4 && DLL_TJSVariant_AsInteger(&first.result) == 42, "integer eval");
    first.eval("1.25 + 2.5");
    require(DLL_TJSVariant_GetType(&first.result) == 5 && DLL_TJSVariant_AsReal(&first.result) == 3.75f, "real eval");
    first.eval("\"hello\" + \" world\"");
    require(first.text() == "hello world", "string eval");
    first.eval("4294967296");
    require(DLL_TJSVariant_AsInteger(&first.result) == 4294967296LL, "64-bit result");
    DLL_TJS_ExecScript(&first.script, "var saved = 40; function plus(v) { return saved + v; }", nullptr);
    first.eval("plus(2)");
    require(DLL_TJSVariant_AsInteger(&first.result) == 42, "persistent globals and function");
    first.eval("Scripts instanceof \"Class\"");
    require(DLL_TJSVariant_AsInteger(&first.result) == 1, "Scripts native class");
    first.eval("Scripts.eval(\"6*7\")");
    require(DLL_TJSVariant_AsInteger(&first.result) == 42, "nested Scripts.eval");
    first.eval("Scripts.eval(\"[3,5,8]\").join(\",\")");
    require(first.text() == "3,5,8", "VM objects survive nested Scripts boundary");
    first.eval("Scripts.eval(\"<% 01 02 %>\").length");
    require(DLL_TJSVariant_AsInteger(&first.result) == 2, "VM octets survive nested Scripts boundary");
    DLL_TJS_ExecScript(&first.script, "Scripts.exec(\"var fromScripts = 21;\", \"ignored name\", 99);", nullptr);
    first.eval("fromScripts*2");
    require(DLL_TJSVariant_AsInteger(&first.result) == 42, "Scripts.exec globals");
    DLL_TJS_ExecScript(&first.script,
        "var badArg = 0; try { Scripts.eval(); } catch(e) { badArg = 1; }", nullptr);
    first.eval("badArg");
    require(DLL_TJSVariant_AsInteger(&first.result) == 1, "Scripts requires source argument");

    CM3D2_FileSystemDelegateHandle provider = nullptr;
    DLL_FileSystemDelegate_Create(&provider);
    DLL_FileSystemDelegate_RegisterGetFileWideCallBack(&provider, scriptFile);
    {
        Fixture storage(DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(&provider));
        storage.eval("Scripts.evalStorage(\"ignored.tjs\", \"eval.tjs\")");
        require(requested_path == L"eval.tjs" && DLL_TJSVariant_AsInteger(&storage.result) == 42,
            "evalStorage opens second argument as native does");
        require(storage_files_alive == 0 && storage_files_destroyed == 1, "evalStorage releases file");
        DLL_TJS_ExecScript(&storage.script, "Scripts.execStorage(\"exec.tjs\", \"ignored name\");", nullptr);
        storage.eval("storageSaved");
        require(requested_path == L"exec.tjs" && DLL_TJSVariant_AsInteger(&storage.result) == 73,
            "execStorage opens first argument and persists globals");
        DLL_TJS_CreateTjsConsoleClass(&storage.script, console);
        const auto errors_before = errors;
        storage.eval("Scripts.evalStorage(\"eval.tjs\")");
        require(requested_path.empty() && errors == errors_before + 1,
            "evalStorage omitted second path attempts empty filename");
        storage.eval("Scripts.evalStorage(\"ignored\", \"empty.tjs\")");
        storage.eval("Scripts.evalStorage(\"ignored\", \"bad.tjs\")");
        storage.eval("Scripts.evalStorage(\"ignored\", \"syntax.tjs\")");
        require(errors == errors_before + 4 && storage_files_alive == 0 && storage_files_destroyed == 5,
            "loader failure paths release owned files");
        errors = 0;
    }
    // The filesystem is borrowed and must outlive its script engines.
    require(DLL_FileSystemDelegate_IsValid(&provider) != 0, "script does not delete provider");
    DLL_FileSystemDelegate_Delete(&provider);
    payload_script = &first.script;
    DLL_TJS_ExecScript(&first.script,
        "var payloadObject = %[\"value\"=>21]; var payloadOctet = <% 00 7f 80 ff %>;"
        "class PayloadReceiver { var value = 41; function get() { return value; } }"
        "var payloadReceiver = new PayloadReceiver(); var payloadClosure = payloadReceiver.get;", nullptr);
    DLL_TJS_AddMemberFunction(&first.script, "payload", payload);
    DLL_TJS_AddMemberFunction(&first.script, "mutatePayload", mutatePayload);
    payload_source = "payloadObject";
    first.eval("payload(payloadObject,payloadOctet) === payloadObject");
    require(DLL_TJSVariant_AsInteger(&first.result) == 1, "object callback identity round trip");
    DLL_TJS_ExecScript(&first.script, "payload(payloadObject,payloadOctet).value = 93;", nullptr);
    first.eval("payloadObject.value");
    require(DLL_TJSVariant_AsInteger(&first.result) == 93, "returned object mutates original");
    payload_source = "payloadClosure";
    first.eval("payload(payloadObject,payloadOctet)()");
    require(DLL_TJSVariant_AsInteger(&first.result) == 41, "bound receiver crosses host callback");
    DLL_TJS_ExecScript(&first.script, "payloadReceiver.value = 57;", nullptr);
    first.eval("payload(payloadObject,payloadOctet)()");
    require(DLL_TJSVariant_AsInteger(&first.result) == 57, "bound receiver remains live");
    payload_source = "payloadOctet";
    first.eval("payload(payloadObject,payloadOctet) === payloadOctet");
    require(DLL_TJSVariant_AsInteger(&first.result) == 1, "octet identity round trip");
    first.eval("payload(payloadObject,payloadOctet)[3]");
    require(DLL_TJSVariant_AsInteger(&first.result) == 255, "octet payload bytes cross callback");
    first.eval("payloadObject");
    require(DLL_TJSVariant_GetType(&first.result) == 1, "owned host result retains object payload");
    DLL_TJSVariant_Clear(&first.result);
    require(DLL_TJSVariant_GetType(&first.result) == 0, "clear releases owned object result");
    first.eval("payloadOctet");
    require(DLL_TJSVariant_GetType(&first.result) == 3, "owned host result retains octet payload");
    DLL_TJSVariant_SetInteger(&first.result, 7);
    require(DLL_TJSVariant_AsInteger(&first.result) == 7, "overwrite releases owned octet result");
    DLL_TJS_ExecScript(&first.script, "mutatePayload(payloadObject);", nullptr);
    require(argument_mutations == 1 && payload_calls == 6, "borrowed callback mutation");
    first.eval("payloadObject.value");
    require(DLL_TJSVariant_AsInteger(&first.result) == 93, "argument slot mutation does not overwrite global");
    payload_script = nullptr;
    DLL_TJS_AddMemberFunction(&first.script, "sum", sum);
    first.eval("sum(10, 20, 12)");
    require(DLL_TJSVariant_AsInteger(&first.result) == 42 && callback_count == 1 && callback_id == first_id,
        "callback arguments, result and owner ID");
    // FNV-1a of UTF-16LE "sum", as derived from FUN_180109210.
    std::uint64_t expected = 0xcbf29ce484222325ull;
    for (const unsigned char byte : {0x73, 0, 0x75, 0, 0x6d, 0}) expected = (expected ^ byte) * 0x100000001b3ull;
    require(callback_key == expected, "callback name key");
    DLL_TJS_ExecScript(&first.script, "sum();", nullptr);
    require(null_results == 1, "statement callback receives null result");
    const unsigned previous_calls = callback_count;
    DLL_TJS_AddMemberFunction(&first.script, "sum", nullptr);
    first.eval("sum(1)");
    require(callback_count == previous_calls, "null registration removes callback");

    first.file({'4', '0', '+', '2'}, true);
    require(DLL_TJSVariant_AsInteger(&first.result) == 42, "CP932 file eval");
    first.file({0xef, 0xbb, 0xbf, '4', '0', '+', '2'}, true);
    require(DLL_TJSVariant_AsInteger(&first.result) == 42, "UTF-8 BOM file eval");
    first.file({0xff, 0xfe, '4', 0, '0', 0, '+', 0, '2', 0}, true);
    require(DLL_TJSVariant_AsInteger(&first.result) == 42, "UTF-16LE BOM file eval");
    first.file({'s', 'a', 'v', 'e', 'd', '=', '7', ';'}, false);
    first.eval("saved");
    require(DLL_TJSVariant_AsInteger(&first.result) == 7, "file exec persists globals");
    first.eval("9");
    first.eval("");
    require(DLL_TJSVariant_AsInteger(&first.result) == 9, "empty source leaves result");

    DLL_TJS_CreateTjsConsoleClass(&first.script, console);
    const auto before_empty = errors;
    first.file({}, true);
    require(errors == before_empty + 1, "valid empty file reports loader error");
    require(DLL_TJSVariant_AsInteger(&first.result) == 9, "empty file leaves result");
    first.file({0xff, 0xfe, 0, 0, '4', 0, 0, 0}, true);
    require(errors > 0, "unsupported UTF-32 file reports error");
    require(DLL_TJSVariant_AsInteger(&first.result) == 9, "file loader failure leaves result");
    first.eval("1 +");
    require(errors >= 2 && last_error.find("TjsScript::EvalExpression error.") == 0,
        "script error reported by wrapper");
    require(DLL_TJSVariant_AsInteger(&first.result) == 9, "script failure leaves result");
    DLL_TJS_DeleteTjsScriptClass(&second.script);
    require(second.script == nullptr, "script delete clears handle");
    first.eval("saved + 1");
    require(DLL_TJSVariant_AsInteger(&first.result) == 8, "other engine survives deletion");
    std::puts("tjs_script_contract ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAILED: %s\n", error.what());
    return 1;
}
