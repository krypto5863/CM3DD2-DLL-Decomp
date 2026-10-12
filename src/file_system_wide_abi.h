#pragma once

// Callable InterfaceFileSystemWide / shared-virtual-base adapter.
// Evidence and slot tables: docs/filesystem-wide-abi-review.md.
//
// This is a SEMANTIC adapter, not a literal copy of the native objects: it allocates a zeroed buffer of the
// native object size, places the wide-interface vptr, its vbptr/vbtable, the virtual-base vptr (with vtordisp
// 0), the mode-flag byte (base-0x50) at the native offsets, and routes every slot to callbacks. A consumer that
// reads the tables the way the original's consumers do (wide->vptr[slot](wide, ...), base located through
// *(int*)(*(wide+8)+4)+wide+8) therefore works against it. The name-map member (base-0x48) is NOT emulated: its
// element type and raw layout are unresolved, so get_map returns the callback's pointer or nullptr.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "file_interface.h"

namespace cm3d2::internal::wide_abi {

// Wide interface vtable (this = the wide pointer: object+0x10, or the delegate object itself).
struct WideVtbl {
    IFile* (*open)(void* self, const wchar_t* name);                   // slot 0, caller owns the result
    bool (*exists)(void* self, const wchar_t* name);                   // slot 1
    // slot 2: CONSTRUCTS the vector in *out (the caller passes raw/zeroed storage) and returns out.
    std::vector<std::wstring>* (*list)(void* self, std::vector<std::wstring>* out, const wchar_t* name, int mode);
    void* (*get_map)(void* self);                                      // slot 3
};

// Shared virtual base vtable (this = the virtual base subobject; the native entries sit behind vtordisp thunks).
struct BaseVtbl {
    void* (*deleting_destructor)(void* self, unsigned flags);          // slot 0: returns the complete object
    bool (*is_valid)(void* self);                                      // slot 1
    bool (*get_flag)(void* self);                                      // slot 2: byte at base-0x50
    bool (*set_flag)(void* self, bool value);                          // slot 3: stores and returns it
    void* (*get_map)(void* self);                                      // slot 4: native returns base-0x48
};

// Narrow interface at +0x20 (Windows and Archive only); names are UTF-8. Declared for completeness, not adapted.
struct NarrowVtbl {
    IFile* (*open)(void* self, const char* name);
    bool (*exists)(void* self, const char* name);
    std::vector<std::string>* (*list)(void* self, std::vector<std::string>* out, const char* name, int mode);
};

struct Layout {
    std::size_t object_size;
    std::size_t wide_offset;       // wide interface subobject
    std::size_t narrow_offset;     // 0 if absent
    std::size_t base_offset;       // shared virtual base
    std::size_t flag_offset;       // base_offset - 0x50
    std::size_t map_offset;        // base_offset - 0x48
};

inline constexpr Layout kWindowsLayout{0x1e8, 0x10, 0x20, 0x1e0, 0x190, 0x198};
inline constexpr Layout kArchiveLayout{0x118, 0x10, 0x20, 0x110, 0xc0, 0xc8};
inline constexpr Layout kDelegateLayout{0x128, 0x00, 0, 0x120, 0xd0, 0xd8};

static_assert(kWindowsLayout.base_offset - 0x50 == kWindowsLayout.flag_offset);
static_assert(kArchiveLayout.base_offset - 0x50 == kArchiveLayout.flag_offset);
static_assert(kDelegateLayout.base_offset - 0x50 == kDelegateLayout.flag_offset);
static_assert(kWindowsLayout.base_offset - 0x48 == kWindowsLayout.map_offset);
static_assert(kArchiveLayout.base_offset - 0x48 == kArchiveLayout.map_offset);
static_assert(kDelegateLayout.base_offset - 0x48 == kDelegateLayout.map_offset);

enum class Placement { Windows, Archive, Delegate };

// Hooks the slots forward to. Any null hook gives the delegate-style default (null / false / empty list / true).
struct Callbacks {
    void* user = nullptr;
    IFile* (*open)(void* user, const wchar_t* name) = nullptr;
    bool (*exists)(void* user, const wchar_t* name) = nullptr;
    void (*list)(void* user, std::vector<std::wstring>& out, const wchar_t* name, int mode) = nullptr;
    void* (*get_file_map)(void* user) = nullptr; // wide slot 3: map<u64,wstring>
    void* (*get_menu_set)(void* user) = nullptr; // base slot 4: set<wstring>
    bool (*get_flag)(void* user) = nullptr;     // optional authoritative backend mode
    bool (*set_flag)(void* user, bool value) = nullptr;
    bool (*is_valid)(void* user) = nullptr;     // default true
    void (*destroy)(void* user) = nullptr;      // runs from the deleting destructor
};

struct Adapter {
    void* object = nullptr;   // start of the allocation
    void* wide = nullptr;     // object + wide_offset
    void* base = nullptr;     // object + base_offset
};

// Allocates and wires an adapter; destroy it through the base vtable slot 0 (flags & 1 frees the buffer).
Adapter createAdapter(Placement placement, const Callbacks& callbacks);

// Locates the virtual base from a wide pointer exactly like the native consumers: *(int*)(*(wide+8)+4) + wide + 8.
void* baseFromWide(void* wide);

inline const WideVtbl* wideVtbl(void* wide) { return *static_cast<const WideVtbl* const*>(wide); }
inline const BaseVtbl* baseVtbl(void* base) { return *static_cast<const BaseVtbl* const*>(base); }

} // namespace cm3d2::internal::wide_abi
