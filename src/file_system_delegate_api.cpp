#include "cm3d2/file_system_delegate_api.h"

#include "file_interface.h"
#include "file_system_facade.h"
#include "file_system_wide_abi.h"
#include "file_system_map_abi.h"

#include <new>
#include <mutex>
#include <unordered_map>

namespace {

namespace abi = cm3d2::internal::wide_abi;
namespace maps = cm3d2::internal::map_abi;

// Callback storage is private; the exposed handle is the callable adapter itself.
// Delegate's two raw containers are permanently empty, so no refresh can retire
// nodes while a native consumer is walking them.
struct FileSystemDelegateObject {
    CM3D2_IsExistentFileCallBack is_existent = nullptr;
    CM3D2_GetFileWideCallBack get_file = nullptr;
    abi::Adapter adapter;
    bool file_map_constructed = false;
    bool menu_set_constructed = false;
    void* fileMap() const { return static_cast<char*>(adapter.object) + 0x10; }
    void* menuSet() const { return static_cast<char*>(adapter.base) - 0x48; }
    ~FileSystemDelegateObject() {
        if (menu_set_constructed) maps::destroy(menuSet(), maps::Kind::Set);
        if (file_map_constructed) maps::destroy(fileMap(), maps::Kind::Map);
    }
};

std::mutex g_delegates_mutex;
std::unordered_map<const void*, FileSystemDelegateObject*> g_delegates;

FileSystemDelegateObject* objectUnlocked(const void* pointer) {
    const auto found = g_delegates.find(pointer);
    return found == g_delegates.end() ? nullptr : found->second;
}

FileSystemDelegateObject* object(CM3D2_FileSystemDelegateHandle* delegate) {
    std::lock_guard<std::mutex> lock(g_delegates_mutex);
    return delegate == nullptr ? nullptr : objectUnlocked(*delegate);
}

cm3d2::internal::IFile* delegateOpen(void* user, const wchar_t* name) {
    CM3D2_GetFileWideCallBack callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_delegates_mutex);
        callback = static_cast<FileSystemDelegateObject*>(user)->get_file;
    }
    // Native wide slot returns the callback result directly; the C export and
    // semantic facade separately reject invalid files.
    return callback == nullptr ? nullptr : static_cast<cm3d2::internal::IFile*>(callback(name));
}
std::uint8_t delegateExistsResult(void* user, const wchar_t* name) {
    CM3D2_IsExistentFileCallBack callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_delegates_mutex);
        callback = static_cast<FileSystemDelegateObject*>(user)->is_existent;
    }
    return callback != nullptr ? callback(name) : 0;
}
bool delegateExists(void* user, const wchar_t* name) { return delegateExistsResult(user, name) != 0; }
void destroyDelegate(void* user) {
    auto* self = static_cast<FileSystemDelegateObject*>(user);
    {
        std::lock_guard<std::mutex> lock(g_delegates_mutex);
        g_delegates.erase(self->adapter.wide);
    }
    delete self;
}
void* delegateFileMap(void* user) { return static_cast<FileSystemDelegateObject*>(user)->fileMap(); }
void* delegateMenuSet(void* user) { return static_cast<FileSystemDelegateObject*>(user)->menuSet(); }

} // namespace

extern "C" {

void CM3D2_CALL DLL_FileSystemDelegate_Create(CM3D2_FileSystemDelegateHandle* out_delegate) {
    if (out_delegate == nullptr) return;
    *out_delegate = nullptr;
    auto* self = new (std::nothrow) FileSystemDelegateObject;
    if (self == nullptr) return;
    try {
        abi::Callbacks callbacks;
        callbacks.user = self;
        callbacks.open = delegateOpen;
        callbacks.exists = delegateExists;
        callbacks.get_file_map = delegateFileMap;
        callbacks.get_menu_set = delegateMenuSet;
        callbacks.destroy = destroyDelegate;
        self->adapter = abi::createAdapter(abi::Placement::Delegate, callbacks);
        if (maps::construct(self->fileMap(), maps::Kind::Map) != maps::Status::Ok) throw std::bad_alloc();
        self->file_map_constructed = true;
        if (maps::construct(self->menuSet(), maps::Kind::Set) != maps::Status::Ok) throw std::bad_alloc();
        self->menu_set_constructed = true;
        std::lock_guard<std::mutex> lock(g_delegates_mutex);
        g_delegates.emplace(self->adapter.wide, self);
        *out_delegate = self->adapter.wide;
    } catch (...) {
        if (self->adapter.object != nullptr)
            abi::baseVtbl(self->adapter.base)->deleting_destructor(self->adapter.base, 1);
        else delete self;
    }
}

// Native Delete (0x18006e0a0) dispatches via the +0x120 virtual base to the
// vtordisp thunk 0x18006e9f0 -> 0x18006de00. The latter frees the WHOLE 0x128
// allocation after destroying its containers/callbacks; 0x18006dee0 is NOT this
// delegate's active deleting-destructor slot. Null output-pointer handling is a
// deliberate normalization; a null value in a valid handle slot is native-safe.
void CM3D2_CALL DLL_FileSystemDelegate_Delete(CM3D2_FileSystemDelegateHandle* delegate) {
    if (delegate == nullptr) return;
    if (auto* self = object(delegate))
        abi::baseVtbl(self->adapter.base)->deleting_destructor(self->adapter.base, 1);
    *delegate = nullptr;
}

// Native returns *handle; the delegate object is the wide interface.
void* CM3D2_CALL DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(
    CM3D2_FileSystemDelegateHandle* delegate) {
    return delegate == nullptr ? nullptr : *delegate;
}

// Static dispatch: +0x120 vtable 0x1801dcb18 slot1 -> 0x18006e9d8 ->
// DLL_FileSystem_TestFlag (0x18006b850), MOV AL,1; RET. Validity is constant for
// a live delegate, not dependent on callbacks/the hierarchy flag. The native
// export dereferences null handles; returning 0 there remains documented hardening.
uint32_t CM3D2_CALL DLL_FileSystemDelegate_IsValid(CM3D2_FileSystemDelegateHandle* delegate) {
    return object(delegate) == nullptr ? 0 : 1;
}

// Native slot 1 (0x180074450): calls the registered callback when the path is
// non-null; otherwise returns false.
uint32_t CM3D2_CALL DLL_FileSystemDelegate_IsExistentFile(
    CM3D2_FileSystemDelegateHandle* delegate,
    const wchar_t* path) {
    const auto* self = object(delegate);
    if (self == nullptr || path == nullptr) return 0;
    return delegateExistsResult(const_cast<FileSystemDelegateObject*>(self), path);
}

// Native: a null callback clears the stored pointer.
void CM3D2_CALL DLL_FileSystemDelegate_RegisterIsExistentFileCallBack(
    CM3D2_FileSystemDelegateHandle* delegate,
    CM3D2_IsExistentFileCallBack callback) {
    std::lock_guard<std::mutex> lock(g_delegates_mutex);
    if (delegate != nullptr) if (auto* self = objectUnlocked(*delegate)) self->is_existent = callback;
}

void CM3D2_CALL DLL_FileSystemDelegate_RegisterGetFileWideCallBack(
    CM3D2_FileSystemDelegateHandle* delegate,
    CM3D2_GetFileWideCallBack callback) {
    std::lock_guard<std::mutex> lock(g_delegates_mutex);
    if (delegate != nullptr) if (auto* self = objectUnlocked(*delegate)) self->get_file = callback;
}

// Native slot 0 (0x180074410) calls the registered retrieval callback. The export
// (0x18006e0e0) hands over the result only if it is valid (slot 5); otherwise it
// releases it through the deleting destructor (slot 0) and reports failure.
uint32_t CM3D2_CALL DDLL_FileSystemDelegate_GetFile(
    CM3D2_FileSystemDelegateHandle* delegate,
    const wchar_t* path,
    CM3D2_FileHandle* out_file) {
    if (out_file == nullptr) return 0;
    *out_file = nullptr;

    const auto* self = object(delegate);
    if (self == nullptr || path == nullptr) return 0;

    const CM3D2_FileHandle file = delegateOpen(const_cast<FileSystemDelegateObject*>(self), path);
    if (file == nullptr) return 0;

    auto* interface_file = static_cast<cm3d2::internal::IFile*>(file);
    if (!interface_file->valid()) {
        delete interface_file;
        return 0;
    }
    *out_file = file;
    return 1;
}

} // extern "C"

namespace cm3d2::internal {
IFile* fileSystemDelegateOpenWide(const void* wide_interface, const std::wstring& name) {
    CM3D2_GetFileWideCallBack callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_delegates_mutex);
        const auto* self = objectUnlocked(wide_interface);
        if (self == nullptr) return nullptr;
        callback = self->get_file;
    }
    // Never invoke host code while holding the registry lock: callbacks may reenter.
    if (callback == nullptr) return nullptr;
    auto* file = static_cast<IFile*>(callback(name.c_str()));
    if (file != nullptr && !file->valid()) {
        delete file;
        return nullptr;
    }
    return file;
}
} // namespace cm3d2::internal
