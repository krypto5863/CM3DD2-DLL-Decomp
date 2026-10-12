#include "cm3d2/file_system_api.h"

#include <Windows.h>

#include "file_factories.h"
#include "file_system_archive.h"
#include "file_system_facade.h"
#include "file_system_windows.h"
#include "file_system_wide_abi.h"
#include "file_system_map_abi.h"
#include "file_system_narrow_abi.h"
#include "warp_archive.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
namespace abi = cm3d2::internal::wide_abi;
namespace maps = cm3d2::internal::map_abi;
namespace narrow = cm3d2::internal::narrow_abi;

struct MapSnapshot {
    alignas(8) unsigned char files[maps::kImageSize]{};
    alignas(8) unsigned char menus[maps::kImageSize]{};
    MapSnapshot() {
        if (maps::construct(files,maps::Kind::Map)!=maps::Status::Ok) throw std::bad_alloc();
        if (maps::construct(menus,maps::Kind::Set)!=maps::Status::Ok) {
            maps::destroy(files,maps::Kind::Map);
            throw std::bad_alloc();
        }
    }
    ~MapSnapshot() {
        maps::destroy(files,maps::Kind::Map);
        maps::destroy(menus,maps::Kind::Set);
    }
};

struct InterfaceToken {
    const void* vtable = nullptr;
    void* owner = nullptr;
};

/*
 * The first 0x30 bytes model the observed multiple-inheritance header. The
 * original wide interface begins at +0x10 and the file-provider interface at
 * +0x20. These tokens deliberately do not claim a reconstructed vtable yet.
 */
struct FileSystemState {
    const void* main_vtable = nullptr;          // +0x00
    const void* hierarchy_metadata = nullptr;   // +0x08
    InterfaceToken wide_interface{};            // +0x10
    InterfaceToken file_provider{};             // +0x20
    CM3D2_FileSystemKind kind = CM3D2_FILE_SYSTEM_WINDOWS;
    // Base directory (+0x30): normalized and ending in a backslash (FUN_1800781d0).
    std::wstring base_directory;
    // FileSystemWindows folders, scan data and auto paths (default mode).
    cm3d2::internal::WindowsFolderSet windows;
    // FileSystemArchive archives, auto paths and per-archive table vectors.
    cm3d2::internal::ArchiveFolderSet archive_set;
    abi::Adapter adapter;
    std::unique_ptr<narrow::NarrowBinding> narrow_binding;
    bool counted = false;
    std::uint64_t map_revision = 0;
    std::uint64_t published_revision = ~std::uint64_t{0};
    std::mutex snapshots_mutex;
    // Never retire a published node while an external reader may hold it.
    std::vector<std::unique_ptr<MapSnapshot>> snapshots;

    explicit FileSystemState(CM3D2_FileSystemKind value) : kind(value) {
        wide_interface.owner = this;
        file_provider.owner = this;
    }
};

static_assert(offsetof(FileSystemState, wide_interface) == 0x10);
static_assert(offsetof(FileSystemState, file_provider) == 0x20);
static_assert(sizeof(CM3D2_FileSystemHandle) == 0x10);

std::atomic<std::int32_t> g_file_system_instance_count{0};
std::mutex g_patch_directories_mutex;
std::vector<std::wstring> g_patch_directories;
std::mutex g_wide_interfaces_mutex;
std::unordered_map<const void*, const FileSystemState*> g_wide_interfaces;

MapSnapshot* snapshot(FileSystemState& object) {
    std::lock_guard<std::mutex> lock(object.snapshots_mutex);
    if (object.snapshots.empty() || object.published_revision!=object.map_revision) {
        auto fresh=std::make_unique<MapSnapshot>();
        const auto* files=object.kind==CM3D2_FILE_SYSTEM_WINDOWS ? &object.windows.rawFileNames()
                                                               : object.archive_set.rawFileNames();
        std::vector<std::pair<std::uint64_t,std::wstring>> entries;
        if (files) for (const auto& entry : files->entries()) entries.emplace_back(entry.first,*entry.second);
        const auto& menus=object.kind==CM3D2_FILE_SYSTEM_WINDOWS ? object.windows.rawMenuNames()
                                                               : object.archive_set.rawMenuNames();
        const auto file_status=maps::assignMap(fresh->files,entries,files ? files->bucketCount() : 8);
        const auto menu_status=maps::assignSet(fresh->menus,menus.values(),menus.bucketCount());
        if (file_status!=maps::Status::Ok || menu_status!=maps::Status::Ok) {
            if (file_status==maps::Status::AllocationFailed || menu_status==maps::Status::AllocationFailed)
                throw std::bad_alloc();
            throw std::runtime_error("filesystem map snapshot geometry/order mismatch");
        }
        object.snapshots.push_back(std::move(fresh));
        object.published_revision=object.map_revision;
    }
    return object.snapshots.back().get();
}

cm3d2::internal::IFile* adapterOpen(void* user,const wchar_t* name) {
    const auto* object=static_cast<FileSystemState*>(user);
    return cm3d2::internal::fileSystemOpenWide(object->adapter.wide,name);
}
bool adapterExists(void* user,const wchar_t* name) {
    const auto* object=static_cast<FileSystemState*>(user);
    if (object->kind==CM3D2_FILE_SYSTEM_WINDOWS) {
        const auto path=object->windows.resolve(name);
        if (path.empty()) return false;
        const auto attributes=GetFileAttributesW(path.c_str());
        return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
    }
    cm3d2::internal::ArchiveFolderSet::Hit hit;
    return object->archive_set.find(name,false,hit,true);
}
void adapterList(void* user,std::vector<std::wstring>& out,const wchar_t* name,int mode) {
    const auto* object=static_cast<FileSystemState*>(user);
    out=object->kind==CM3D2_FILE_SYSTEM_WINDOWS ? object->windows.list(name,mode) : object->archive_set.list(name,mode);
}
bool adapterValid(void* user) {
    const auto* object=static_cast<FileSystemState*>(user);
    return object->kind==CM3D2_FILE_SYSTEM_WINDOWS ? object->windows.valid() : object->archive_set.valid();
}
bool adapterFlag(void* user) {
    const auto* object=static_cast<FileSystemState*>(user);
    return object->kind==CM3D2_FILE_SYSTEM_WINDOWS ? object->windows.treeMode() : object->archive_set.treeMode();
}
bool adapterSetFlag(void* user,bool value) {
    auto* object=static_cast<FileSystemState*>(user);
    if (object->kind==CM3D2_FILE_SYSTEM_WINDOWS) object->windows.setTreeMode(value);
    else object->archive_set.setTreeMode(value);
    return value;
}
void* adapterFiles(void* user) { return snapshot(*static_cast<FileSystemState*>(user))->files; }
void* adapterMenus(void* user) { return snapshot(*static_cast<FileSystemState*>(user))->menus; }
void adapterDestroy(void* user) {
    auto* object=static_cast<FileSystemState*>(user);
    {
        std::lock_guard<std::mutex> lock(g_wide_interfaces_mutex);
        g_wide_interfaces.erase(object->adapter.wide);
        g_wide_interfaces.erase(&object->wide_interface);
    }
    if (object->counted) g_file_system_instance_count.fetch_sub(1,std::memory_order_relaxed);
    delete object;
}

bool supported(const CM3D2_FileSystemHandle* handle) {
    return handle != nullptr &&
        handle->object != nullptr &&
        (handle->kind == CM3D2_FILE_SYSTEM_WINDOWS ||
         handle->kind == CM3D2_FILE_SYSTEM_ARCHIVE);
}

// FUN_18008feb0 returns FileMemory, not a separate streaming member class.
// Transfer the decoder's final allocation directly; the mounted stream is
// retained and its cursor restored under the archive reader's global mutex.
// Decoder failure/layout differences remain documented in the member review.
cm3d2::internal::IFile* openArchiveMemory(const cm3d2::internal::WarcArchive& archive,
                                       std::uint64_t value) {
    std::unique_ptr<std::byte[]> data;
    std::uint32_t size = 0;
    if (!archive.readOwned(value, data, size)) return nullptr;
    return cm3d2::internal::makeFileMemoryOwned(data.release(), size);
}

cm3d2::internal::IFile* archiveOpenHashes(void* user,const std::vector<std::uint64_t>& keys) {
    const auto* object=static_cast<FileSystemState*>(user);
    cm3d2::internal::ArchiveFolderSet::Hit hit;
    return object->archive_set.findHashes(keys,true,hit) ? openArchiveMemory(*hit.archive,hit.value) : nullptr;
}
bool archiveExistsHashes(void* user,const std::vector<std::uint64_t>& keys) {
    const auto* object=static_cast<FileSystemState*>(user);
    cm3d2::internal::ArchiveFolderSet::Hit hit;
    return object->archive_set.findHashes(keys,false,hit);
}

FileSystemState* state(CM3D2_FileSystemHandle* handle) {
    return supported(handle) ? static_cast<FileSystemState*>(handle->object) : nullptr;
}

const FileSystemState* state(const CM3D2_FileSystemHandle* handle) {
    return supported(handle) ? static_cast<const FileSystemState*>(handle->object) : nullptr;
}

// The narrow exports convert UTF-8 with std::wstring_convert<codecvt_utf8> and call the wide
// virtual; a null pointer becomes an empty string.
std::wstring utf8ToWide(const char* value) {
    return narrow::utf8ToWide(value);
}

// FUN_180077ca0: the Windows AddArchive path converts its narrow text with code page 932.
std::wstring cp932ToWide(const char* value) {
    if (value == nullptr || *value == '\0') {
        return {};
    }
    const int required = MultiByteToWideChar(932, 0, value, -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(932, 0, value, -1, wide.data(), required);
    wide.resize(static_cast<std::size_t>(required - 1));
    return wide;
}

void createFileSystem(
    CM3D2_FileSystemHandle* out_file_system,
    CM3D2_FileSystemKind kind) {
    if (out_file_system == nullptr) {
        return;
    }
    auto* object = new FileSystemState(kind);
    try {
        abi::Callbacks callbacks;
        callbacks.user=object;
        callbacks.open=adapterOpen; callbacks.exists=adapterExists; callbacks.list=adapterList;
        callbacks.get_file_map=adapterFiles; callbacks.get_menu_set=adapterMenus;
        callbacks.is_valid=adapterValid; callbacks.get_flag=adapterFlag; callbacks.set_flag=adapterSetFlag;
        callbacks.destroy=adapterDestroy;
        object->adapter=abi::createAdapter(kind==CM3D2_FILE_SYSTEM_WINDOWS ? abi::Placement::Windows : abi::Placement::Archive,callbacks);
        narrow::Hooks hooks;
        hooks.user=object; hooks.open_hashes=archiveOpenHashes; hooks.exists_hashes=archiveExistsHashes;
        object->narrow_binding=std::make_unique<narrow::NarrowBinding>(object->adapter.object,
            kind==CM3D2_FILE_SYSTEM_WINDOWS ? abi::Placement::Windows : abi::Placement::Archive,
            kind==CM3D2_FILE_SYSTEM_WINDOWS ? narrow::Mode::ForwardToWide : narrow::Mode::ArchiveHashes,hooks);
        snapshot(*object); // initial empty generation; getters always have a valid container
        std::lock_guard<std::mutex> lock(g_wide_interfaces_mutex);
        g_wide_interfaces.emplace(object->adapter.wide,object);
        g_wide_interfaces.emplace(&object->wide_interface, object);
    } catch (...) {
        if (object->adapter.object) abi::baseVtbl(object->adapter.base)->deleting_destructor(object->adapter.base,1);
        else delete object;
        throw;
    }
    out_file_system->object = object;
    // The original writes only the 12 bytes object + kind into the caller's slot (abi_canary_probe); the trailing
    // `reserved` member of the C struct is never written.
    out_file_system->kind = kind;
    g_file_system_instance_count.fetch_add(1, std::memory_order_relaxed);
    object->counted=true;
}

// Narrow lookups (FUN_18008db40 and FUN_18008e420) hash the UTF-8 name and search the tag 1
// tables of the mounted archives, newest archive first.
bool findInArchives(
    const FileSystemState& object,
    const char* utf8_name,
    bool for_get_file,
    cm3d2::internal::ArchiveFolderSet::Hit& hit) {
    if (utf8_name == nullptr || *utf8_name == '\0') {
        return false;
    }
    return object.archive_set.findHashes(narrow::archiveComponentHashes(utf8_name), for_get_file, hit);
}

std::string wideToUtf8(const std::wstring& wide) {
    return narrow::wideToUtf8(wide);
}

using StringVector = std::vector<std::string>;

// The list exports hand the caller a heap vector and its count (struct at out_list).
void publishList(CM3D2_FileSystemList* out_list, StringVector* strings) {
    out_list->strings = strings;
    out_list->count = static_cast<std::int32_t>(strings->size());
}

// The names a file system lists for a folder name (provider slot 2, FUN_18007b210 -> wide list).
// A FileSystemArchive uses FUN_18008c410.
StringVector listNames(const FileSystemState& object, const char* utf8_name, int mode) {
    StringVector out;
    const auto names = object.kind == CM3D2_FILE_SYSTEM_WINDOWS
        ? object.windows.list(utf8ToWide(utf8_name), mode)
        : object.archive_set.list(utf8ToWide(utf8_name), mode);
    for (const auto& name : names) {
        out.push_back(wideToUtf8(name));
    }
    return out;
}

} // namespace

extern "C" {

void CM3D2_CALL DLL_FileSystem_CreateFileSystemWindows(
    CM3D2_FileSystemHandle* out_file_system) {
    createFileSystem(out_file_system, CM3D2_FILE_SYSTEM_WINDOWS);
}

void CM3D2_CALL DLL_FileSystem_CreateFileSystemArchive(
    CM3D2_FileSystemHandle* out_file_system) {
    createFileSystem(out_file_system, CM3D2_FILE_SYSTEM_ARCHIVE);
}

void CM3D2_CALL DLL_FileSystem_DeleteFileSystem(CM3D2_FileSystemHandle* file_system) {
    auto* object = state(file_system);
    if (object != nullptr)
        abi::baseVtbl(object->adapter.base)->deleting_destructor(object->adapter.base,1);
    else g_file_system_instance_count.fetch_sub(1,std::memory_order_relaxed);
}

// Base-class slot 0x28 (FUN_180078390) -> wide slot 0x20 (FUN_1800781d0): the name needs a ':'
// at index 1 or later; it is normalized and a backslash is appended.
uint32_t CM3D2_CALL DLL_FileSystem_SetBaseDirectory(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path) {
    auto* object = state(file_system);
    if (object == nullptr) {
        return 0;
    }
    std::wstring name = utf8ToWide(utf8_path);
    if (name.size() < 2 || name.find(L':', 1) == std::wstring::npos) {
        return 0;
    }
    object->base_directory = cm3d2::internal::normalizeFileSystemName(std::move(name)) + L'\\';
    return 1;
}

// Windows slot 0x40 (FUN_180078ac0) -> wide slot 0x38 (FUN_180078580). On a FileSystemArchive the
// same slot is its wide archive loader, which is handed this narrow pointer; that is not modelled.
uint32_t CM3D2_CALL DLL_FileSystem_AddFolder(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path) {
    auto* object = state(file_system);
    if (object == nullptr || object->kind != CM3D2_FILE_SYSTEM_WINDOWS) {
        return 0;
    }
    ++object->map_revision;
    return object->windows.addFolder(utf8ToWide(utf8_path), object->base_directory) ? 1 : 0;
}

uint32_t CM3D2_CALL DLL_FileSystem_AddArchive(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path) {
    auto* object = state(file_system);
    if (object == nullptr) {
        return 0;
    }
    if (object->kind == CM3D2_FILE_SYSTEM_WINDOWS) {
        // Windows slot 0x48 (FUN_180078c10): CP932 text goes to the auto-path list (FUN_180078cb0).
        return object->windows.addAutoPath(cp932ToWide(utf8_path)) ? 1 : 0;
    }
    // FileSystemArchive slot 0x48 (FUN_18008b670) -> wide loader FUN_18008ab30.
    if (utf8_path == nullptr) {
        return 0;
    }
    std::wstring name = cm3d2::internal::normalizeFileSystemName(utf8ToWide(utf8_path));
    if (!cm3d2::internal::hasDriveSeparator(name)) {
        name = object->base_directory + name;
    }
    // FUN_18008ab30: a warp archive is converted to a warc archive on disk before it is mounted.
    cm3d2::internal::WarpContext warp;
    warp.base_directory = object->base_directory;
    {
        std::lock_guard<std::mutex> lock(g_patch_directories_mutex);
        warp.preferred_directories = g_patch_directories;
    }
    warp.read_mounted_head = [object](std::uint64_t id, unsigned char* head, std::size_t size) {
        return object->archive_set.readMountedHead(id, head, size);
    };
    if (cm3d2::internal::convertWarpArchive(utf8ToWide(utf8_path), name, warp) == cm3d2::internal::WarpOutcome::Failed) {
        return 0;
    }
    ++object->map_revision;
    return object->archive_set.addArchive(name) ? 1 : 0;
}

// Windows slot 0x58 (FUN_180078fd0) -> FUN_180078cb0; FileSystemArchive slot 0x68
// (FUN_18008c290 -> FUN_18008bf90).
uint32_t CM3D2_CALL DLL_FileSystem_AddAutoPath(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path) {
    auto* object = state(file_system);
    if (object == nullptr) {
        return 0;
    }
    if (object->kind == CM3D2_FILE_SYSTEM_ARCHIVE) {
        return object->archive_set.addAutoPath(utf8ToWide(utf8_path)) ? 1 : 0;
    }
    return object->windows.addAutoPath(utf8ToWide(utf8_path)) ? 1 : 0;
}

// FUN_180079120 and FUN_18008b7a0 only act when the +0x190/+0xc0 flag is set, and that flag is
// false for a fresh file system. Both dispatches include their internal enabled
// job paths; native callable wide-interface/virtual-base layout remains separate.
void CM3D2_CALL DLL_FileSystem_AddAutoPathForAllFolder(
    CM3D2_FileSystemHandle* file_system,
    uint8_t multithread_processing) {
    auto* object = state(file_system);
    if (object && object->kind == CM3D2_FILE_SYSTEM_WINDOWS)
        object->windows.addAutoPathForAllFolder(multithread_processing != 0);
    else if (object && object->kind == CM3D2_FILE_SYSTEM_ARCHIVE)
        object->archive_set.addAutoPathForAllFolder(multithread_processing != 0);
}

// With the flag clear, both file systems return 1 immediately. Windows waiting
// checks eight completion flags and rechecks mode after each native 16ms sleep.
uint32_t CM3D2_CALL DLL_FileSystem_IsFinishedAddAutoPathJob(
    CM3D2_FileSystemHandle* file_system,
    uint8_t wait) {
    const auto* object = state(file_system);
    if (object && object->kind == CM3D2_FILE_SYSTEM_WINDOWS)
        return object->windows.isFinishedAddAutoPathJob(wait != 0) ? 1 : 0;
    if (object && object->kind == CM3D2_FILE_SYSTEM_ARCHIVE)
        return object->archive_set.isFinishedAddAutoPathJob(wait != 0) ? 1 : 0;
    return 1;
}

// Clear root mutex records, without joining workers or resetting completion.
void CM3D2_CALL DLL_FileSystem_ReleaseAddAutoPathJob(
    CM3D2_FileSystemHandle* file_system) {
    auto* object = state(file_system);
    if (object && object->kind == CM3D2_FILE_SYSTEM_WINDOWS)
        object->windows.releaseAddAutoPathJob();
    else if (object && object->kind == CM3D2_FILE_SYSTEM_ARCHIVE)
        object->archive_set.releaseAddAutoPathJob();
}

// FUN_180074da0 / FUN_1800895b0: the folder (or archive) list is not empty.
uint32_t CM3D2_CALL DLL_FileSystem_IsValid(CM3D2_FileSystemHandle* file_system) {
    const auto* object = state(file_system);
    if (object == nullptr) {
        return 0;
    }
    return object->kind == CM3D2_FILE_SYSTEM_WINDOWS ? object->windows.valid() : object->archive_set.valid();
}

uint32_t CM3D2_CALL DLL_FileSystem_IsExistentFile(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_name) {
    const auto* object = state(file_system);
    if (object == nullptr) {
        return 0;
    }
    // FileSystemWindows (provider slot 1 -> FUN_18007b980) resolves the name and tests the path
    // with GetFileAttributesW; FileSystemArchive (FUN_18008e420 -> FUN_18008dfc0) searches archives.
    if (object->kind == CM3D2_FILE_SYSTEM_WINDOWS) {
        const std::wstring path = object->windows.resolve(utf8ToWide(utf8_name));
        if (path.empty()) {
            return 0;
        }
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
    cm3d2::internal::ArchiveFolderSet::Hit hit;
    return findInArchives(*object, utf8_name, false, hit) ? 1 : 0;
}

uint32_t CM3D2_CALL DLL_FileSystem_GetFile(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_name,
    CM3D2_FileHandle* out_file) {
    if (out_file == nullptr) {
        return 0;
    }
    *out_file = nullptr;
    const auto* object = state(file_system);
    if (object == nullptr) {
        return 0;
    }

    if (object->kind == CM3D2_FILE_SYSTEM_ARCHIVE) {
        cm3d2::internal::ArchiveFolderSet::Hit hit;
        if (!findInArchives(*object, utf8_name, true, hit)) {
            return 0;
        }
        auto* file = openArchiveMemory(*hit.archive, hit.value);
        if (file == nullptr || !file->valid()) { delete file; return 0; }
        cm3d2::internal::noteFileInstanceCreated();
        *out_file = file;
        return 1;
    }

    // FileSystemWindows provider slot 0 (FUN_18007b790 -> FUN_18007b650): resolve the path and
    // open it as a FileNormalWindows; an invalid file is deleted and the call fails.
    const std::wstring path = object->windows.resolve(utf8ToWide(utf8_name));
    if (path.empty()) {
        return 0;
    }
    auto* file = cm3d2::internal::openFileNormalWindows(path);
    if (file == nullptr) {
        return 0;
    }
    cm3d2::internal::noteFileInstanceCreated();
    *out_file = file;
    return 1;
}

void* CM3D2_CALL DLL_FileSystem_GetInterfaceFileSystemWidePointer(
    CM3D2_FileSystemHandle* file_system) {
    auto* object = state(file_system);
    return object == nullptr ? nullptr : object->adapter.wide;
}

void CM3D2_CALL DLL_FileSystem_CreateList(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_name,
    int32_t mode,
    CM3D2_FileSystemList* out_list) {
    if (out_list == nullptr) {
        return;
    }
    const auto* object = state(file_system);
    publishList(out_list, new StringVector(object == nullptr ? StringVector{} : listNames(*object, utf8_name, mode)));
}

// "*" lists every name; any other text keeps the names whose extension (the text after the last
// '.', trailing spaces trimmed) equals it exactly (FUN_180066b90, FUN_18006ca30).
void CM3D2_CALL DLL_FileSystem_CreateExtensionList(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_extension,
    CM3D2_FileSystemList* out_list) {
    if (out_list == nullptr) {
        return;
    }
    const auto* object = state(file_system);
    StringVector all = object == nullptr ? StringVector{} : listNames(*object, "", 3);
    const std::string wanted = utf8_extension == nullptr ? std::string() : std::string(utf8_extension);
    auto* result = new StringVector();
    for (auto& name : all) {
        if (wanted == "*") {
            result->push_back(std::move(name));
            continue;
        }
        const auto dot = name.rfind('.');
        std::string extension = dot == std::string::npos ? std::string() : name.substr(dot + 1);
        while (!extension.empty() && extension.back() == ' ') {
            extension.pop_back();
        }
        if (extension == wanted) {
            result->push_back(std::move(name));
        }
    }
    publishList(out_list, result);
}

// FUN_18006b1c0: copies at most buffer_size - 1 bytes and returns the number copied.
uint32_t CM3D2_CALL DLL_FileSystem_AtList(
    CM3D2_FileSystemList* list,
    int32_t index,
    char* buffer,
    int32_t buffer_size) {
    if (list == nullptr || list->strings == nullptr || buffer == nullptr || buffer_size <= 0) {
        return 0;
    }
    const auto& strings = *static_cast<const StringVector*>(list->strings);
    if (index < 0 || static_cast<std::size_t>(index) >= strings.size()) {
        return 0;
    }
    const std::string& text = strings[static_cast<std::size_t>(index)];
    std::size_t length = text.size();
    if (static_cast<std::int64_t>(buffer_size) <= static_cast<std::int64_t>(length)) {
        length = static_cast<std::size_t>(buffer_size - 1);
    }
    std::memcpy(buffer, text.data(), length);
    buffer[length] = '\0';
    return static_cast<uint32_t>(length);
}

void CM3D2_CALL DLL_FileSystem_DeleteList(CM3D2_FileSystemList* list) {
    if (list != nullptr) {
        delete static_cast<StringVector*>(list->strings);
        list->strings = nullptr;
        list->count = 0;
    }
}

// FUN_18006b340: the file-name map of the file system (wide slot 3), optionally keeping only the
// names whose extension equals the extension of file_name. A FileSystemArchive returns the name map
// of its last archive (FUN_18008dfa0).
void CM3D2_CALL DLL_FileSystem_GetNameMapLastArchive(
    CM3D2_FileSystemHandle* file_system,
    const wchar_t* file_name,
    CM3D2_FileSystemList* out_list) {
    if (out_list == nullptr) {
        return;
    }
    auto* result = new StringVector();
    const auto* object = state(file_system);
    if (object != nullptr) {
        std::wstring wanted;
        if (file_name != nullptr) {
            const std::wstring text = file_name;
            const auto dot = text.rfind(L'.');
            if (dot != std::wstring::npos) {
                wanted = text.substr(dot + 1);
                while (!wanted.empty() && wanted.back() == L' ') {
                    wanted.pop_back();
                }
            }
        }
        const auto names = object->kind == CM3D2_FILE_SYSTEM_WINDOWS
            ? object->windows.fileNameMap()
            : object->archive_set.nameMapLastArchive();
        for (const auto& name : names) {
            if (!wanted.empty()) {
                const auto dot = name.rfind(L'.');
                std::wstring extension = dot == std::wstring::npos ? std::wstring() : name.substr(dot + 1);
                while (!extension.empty() && extension.back() == L' ') {
                    extension.pop_back();
                }
                if (extension != wanted) {
                    continue;
                }
            }
            result->push_back(wideToUtf8(name));
        }
    }
    publishList(out_list, result);
}

void CM3D2_CALL DLL_FileSystem_AddPatchDecryptPreferredSearchDirectory(
    const char* cp932_path) {
    // Native: CP932 to wide, ASCII-lowercased, appended only when non-empty.
    auto converted = cp932ToWide(cp932_path);
    if (converted.empty()) {
        return;
    }
    for (auto& ch : converted) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
        }
    }
    std::lock_guard<std::mutex> lock(g_patch_directories_mutex);
    g_patch_directories.push_back(std::move(converted));
}

void CM3D2_CALL DLL_FileSystem_ClearPatchDecryptPreferredSearchDirectory(void) {
    std::lock_guard<std::mutex> lock(g_patch_directories_mutex);
    g_patch_directories.clear();
}

// FUN_18007f450 / FUN_18007fe80: only the FileSystemWindows kind has a cache; the path is UTF-8.
void CM3D2_CALL DLL_FileSystem_CasheSerialize(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path) {
    const auto* object = state(file_system);
    if (object != nullptr && object->kind == CM3D2_FILE_SYSTEM_WINDOWS) {
        object->windows.serialize(utf8ToWide(utf8_path), object->base_directory);
    }
}

uint32_t CM3D2_CALL DLL_FileSystem_CasheDeserialize(
    CM3D2_FileSystemHandle* file_system,
    const char* utf8_path) {
    auto* object = state(file_system);
    if (object == nullptr || object->kind != CM3D2_FILE_SYSTEM_WINDOWS) {
        return 0;
    }
    ++object->map_revision;
    return object->windows.deserialize(utf8ToWide(utf8_path), object->base_directory) ? 1 : 0;
}

uint32_t CM3D2_CALL DLL_FileSystem_TestFlag(void) {
    return 1;
}

} // extern "C"

namespace cm3d2::internal {
namespace {

const FileSystemState* stateOf(const void* wide_interface) {
    std::lock_guard<std::mutex> lock(g_wide_interfaces_mutex);
    const auto found = g_wide_interfaces.find(wide_interface);
    return found == g_wide_interfaces.end() ? nullptr : found->second;
}

} // namespace

IFile* fileSystemOpenWide(const void* wide_interface, const std::wstring& name) {
    const auto* object = stateOf(wide_interface);
    if (object == nullptr) {
        return fileSystemDelegateOpenWide(wide_interface, name);
    }
    if (object->kind == CM3D2_FILE_SYSTEM_WINDOWS) {
        const std::wstring path = object->windows.resolve(name);
        return path.empty() ? nullptr : openFileNormalWindows(path);
    }
    ArchiveFolderSet::Hit hit;
    if (!object->archive_set.find(name, true, hit, true)) return nullptr;
    return openArchiveMemory(*hit.archive, hit.value);
}

std::vector<std::wstring> fileSystemListWide(const void* wide_interface, const std::wstring& name, int mode) {
    const auto* object = stateOf(wide_interface);
    if (object == nullptr) {
        return {};
    }
    return object->kind == CM3D2_FILE_SYSTEM_WINDOWS ? object->windows.list(name, mode)
                                                     : object->archive_set.list(name, mode);
}

std::vector<std::wstring> fileSystemMenuNames(const void* wide_interface) {
    const auto* object = stateOf(wide_interface);
    if (object == nullptr) {
        return {};
    }
    return object->kind == CM3D2_FILE_SYSTEM_WINDOWS ? object->windows.menuFiles()
                                                     : object->archive_set.menuFiles();
}

} // namespace cm3d2::internal
