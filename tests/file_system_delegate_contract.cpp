#include "cm3d2/file_api.h"
#include "cm3d2/file_system_delegate_api.h"
#include "file_system_wide_abi.h"

#include <cstdint>
#include <cwchar>
#include <stdexcept>
#include <cstring>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const wchar_t* g_last_path = nullptr;
CM3D2_FileHandle g_next_file = nullptr;
CM3D2_FileSystemDelegateHandle* g_reentrant_delegate = nullptr;

uint8_t reentrantExists(const wchar_t*) {
    require(DLL_FileSystemDelegate_IsValid(g_reentrant_delegate) == 1,"callback reentry validity");
    DLL_FileSystemDelegate_RegisterIsExistentFileCallBack(g_reentrant_delegate,nullptr);
    return 1;
}

std::uint64_t rawWord(const void* data, std::size_t offset) {
    std::uint64_t word;
    std::memcpy(&word,static_cast<const char*>(data)+offset,sizeof word);
    return word;
}

uint8_t existsCallBack(const wchar_t* path) {
    g_last_path = path;
    return 1;
}

uint8_t existsRejectingCallBack(const wchar_t*) {
    return 0;
}

CM3D2_FileHandle returnNextFile(const wchar_t* path) {
    g_last_path = path;
    return g_next_file;
}

} // namespace

int main() {
    CM3D2_FileSystemDelegateHandle delegate = nullptr;
    DLL_FileSystemDelegate_Create(&delegate);
    require(delegate != nullptr, "creation returned null");
    require(DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(&delegate) == delegate,
        "wide pointer should be the delegate object");
    require(DLL_FileSystemDelegate_IsValid(&delegate) == 1, "live delegate should be valid");
    namespace abi = cm3d2::internal::wide_abi;
    void* wide = DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(&delegate);
    auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(static_cast<char*>(wide)+8);
    void* base = static_cast<char*>(wide)+8+vbtable[1];
    const auto* wide_table = abi::wideVtbl(wide);
    const auto* base_table = abi::baseVtbl(base);
    require(base == static_cast<char*>(wide)+0x120,"delegate virtual base placement");
    require(base_table->is_valid(base),"callable validity");
    require(!base_table->get_flag(base) && base_table->set_flag(base,true) && base_table->get_flag(base),
        "callable mode flag");
    require(!base_table->set_flag(base,false),"clear callable mode flag");
    void* file_map = wide_table->get_map(wide);
    void* menu_set = base_table->get_map(base);
    require(file_map==static_cast<char*>(wide)+0x10 && menu_set==static_cast<char*>(base)-0x48,
        "embedded raw map addresses");
    for (void* image : {file_map,menu_set}) {
        require(rawWord(image,0x10)==0 && rawWord(image,0x30)==7 && rawWord(image,0x38)==8,
            "empty raw container geometry");
        const auto* head=reinterpret_cast<const void*>(rawWord(image,8));
        require(rawWord(head,0)==reinterpret_cast<std::uintptr_t>(head) &&
            rawWord(head,8)==reinterpret_cast<std::uintptr_t>(head),"empty sentinel links");
    }
    alignas(std::vector<std::wstring>) unsigned char storage[sizeof(std::vector<std::wstring>)];
    auto* names=reinterpret_cast<std::vector<std::wstring>*>(storage);
    require(wide_table->list(wide,names,L"",3)==names && names->empty(),"callable empty delegate list");
    names->~vector();

    // Without callbacks, both queries fail and GetFile clears its output.
    require(DLL_FileSystemDelegate_IsExistentFile(&delegate, L"a.txt") == 0,
        "unregistered existence query should be false");
    CM3D2_FileHandle out = reinterpret_cast<CM3D2_FileHandle>(uintptr_t{1});
    require(DDLL_FileSystemDelegate_GetFile(&delegate, L"a.txt", &out) == 0 && out == nullptr,
        "unregistered GetFile should fail and clear the output");

    // A registered existence callback receives the path unchanged.
    DLL_FileSystemDelegate_RegisterIsExistentFileCallBack(&delegate, existsCallBack);
    require(DLL_FileSystemDelegate_IsExistentFile(&delegate, L"a.txt") == 1,
        "registered callback result lost");
    require(g_last_path != nullptr && std::wcscmp(g_last_path, L"a.txt") == 0,
        "callback did not receive the path");
    require(wide_table->exists(wide,L"via-slot.txt"),"callable existence callback");
    g_last_path = nullptr;
    require(DLL_FileSystemDelegate_IsExistentFile(&delegate, nullptr) == 0,
        "null path must not reach the callback");
    require(g_last_path == nullptr, "null path invoked the callback");

    DLL_FileSystemDelegate_RegisterIsExistentFileCallBack(&delegate, existsRejectingCallBack);
    require(DLL_FileSystemDelegate_IsExistentFile(&delegate, L"a.txt") == 0,
        "rejecting callback should report false");
    DLL_FileSystemDelegate_RegisterIsExistentFileCallBack(&delegate, nullptr);
    require(DLL_FileSystemDelegate_IsExistentFile(&delegate, L"a.txt") == 0,
        "null registration should clear the callback");

    // Retrieval: a valid file is handed over, the caller owns it afterwards.
    DLL_FileSystemDelegate_RegisterGetFileWideCallBack(&delegate, returnNextFile);
    CM3D2_FileHandle memory = nullptr;
    DLL_FileMemory_CreateileMemory(&memory);
    const char bytes[] = "xyz";
    DLL_FileMemory_SetData(&memory, bytes, 3);
    g_next_file = DLL_FileMemory_GetInterfaceFilePointer(&memory);
    require(DDLL_FileSystemDelegate_GetFile(&delegate, L"a.txt", &out) == 1 && out == g_next_file,
        "valid file should be returned");
    DLL_FileMemory_DeleteileMemory(&out);

    CM3D2_FileHandle direct_memory=nullptr;
    DLL_FileMemory_CreateileMemory(&direct_memory);
    DLL_FileMemory_SetData(&direct_memory,bytes,3);
    g_next_file=direct_memory;
    auto* direct=wide_table->open(wide,L"via-slot.txt");
    require(direct==direct_memory && direct->valid() && direct->size()==3,"callable retrieval callback");
    CM3D2_FileHandle direct_handle=direct;
    DLL_File_CloseFile(&direct_handle);

    // A callback may call exports/register callbacks without a registry-lock deadlock.
    g_reentrant_delegate=&delegate;
    DLL_FileSystemDelegate_RegisterIsExistentFileCallBack(&delegate,reentrantExists);
    require(wide_table->exists(wide,L"reentry") && !wide_table->exists(wide,L"cleared"),"callback reentry");

    // An invalid file (no data) is rejected and the delegate releases it.
    CM3D2_FileHandle empty = nullptr;
    DLL_FileMemory_CreateileMemory(&empty);
    require(DLL_File_IsValid(&empty) == 0, "empty memory file should be invalid");
    g_next_file = empty;
    require(DDLL_FileSystemDelegate_GetFile(&delegate, L"b.txt", &out) == 0 && out == nullptr,
        "invalid file must not be returned");
    CM3D2_FileHandle direct_empty=nullptr;
    DLL_FileMemory_CreateileMemory(&direct_empty);
    g_next_file=direct_empty;
    auto* invalid_direct=wide_table->open(wide,L"invalid-direct");
    require(invalid_direct==direct_empty && !invalid_direct->valid(),
        "wide open returns invalid callback result without C-export filtering");
    DLL_File_CloseFile(&direct_empty);

    // Null path or null output never reaches the callback.
    g_last_path = nullptr;
    require(DDLL_FileSystemDelegate_GetFile(&delegate, nullptr, &out) == 0 && g_last_path == nullptr,
        "null path must not reach the retrieval callback");

    DLL_FileSystemDelegate_Delete(&delegate);
    require(delegate == nullptr, "delete should clear the handle");
    DLL_FileSystemDelegate_Delete(nullptr);

    // External callers may use the deleting destructor directly; registry entries
    // and raw containers must be released through the same ownership path.
    CM3D2_FileSystemDelegateHandle external=nullptr;
    DLL_FileSystemDelegate_Create(&external);
    void* external_wide=DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(&external);
    auto* external_vb=*reinterpret_cast<const std::int32_t* const*>(static_cast<char*>(external_wide)+8);
    void* external_base=static_cast<char*>(external_wide)+8+external_vb[1];
    require(abi::baseVtbl(external_base)->deleting_destructor(external_base,1)==external_wide,
        "direct deleting destructor returns complete object");
    require(DLL_FileSystemDelegate_IsValid(&external)==0,"direct destruction unregisters facade");
    DLL_FileSystemDelegate_Delete(&external);
    require(external==nullptr,"stale handle normalization does not double delete");
    return 0;
}
