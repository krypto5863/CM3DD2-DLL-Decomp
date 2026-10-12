#pragma once

// Callable narrow (UTF-8) file-system interface at complete object +0x20 (Windows and Archive file systems only).
// Evidence and slot tables: docs/filesystem-narrow-abi-review.md.
//
// The subobject is attached to an object buffer created by wide_abi::createAdapter (same placement: wide at +0x10,
// narrow at +0x20, shared virtual base at +0x1e0 / +0x110). Its vptr table is wide_abi::NarrowVtbl. Like the original
// it owns no state of its own: Windows mode converts the UTF-8 name and calls the WIDE table at this-0x10; Archive mode
// hashes the narrow bytes directly (FUN_18008e9d0) and hands the component hashes to hooks. list() forwards through the
// wide table in both modes (the original shares one function, 0x18007b210, between the two classes).

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "file_system_wide_abi.h"

namespace cm3d2::internal::narrow_abi {

enum class Mode {
    ForwardToWide,   // Windows: open/exists convert UTF-8 -> wide and call wide slots 0/1
    ArchiveHashes,   // Archive: open/exists normalise the narrow bytes into component hashes and use the hooks
};

struct Hooks {
    void* user = nullptr;
    // ArchiveHashes mode only. A null name yields an empty vector (-> null / false without calling the hooks); an empty or trim-to-nothing name yields ONE component, the empty-name hash.
    IFile* (*open_hashes)(void* user, const std::vector<std::uint64_t>& components) = nullptr;
    bool (*exists_hashes)(void* user, const std::vector<std::uint64_t>& components) = nullptr;
};

// UTF-8 -> wide / wide -> UTF-8 exactly as the original's wstring_convert<codecvt_utf8<wchar_t,0x10ffff>> (msvc_utf8_facet.h): decoding has no
// overlong check, accepts 4/5/6-byte forms up to U+10FFFF and keeps only the low 16 bits, passes surrogates through; stray continuation bytes,
// bad continuation bytes, values above 0x10FFFF and truncated sequences throw std::range_error("bad conversion"). Encoding never fails (3 bytes
// per unit >= 0x800, surrogate units individually). A null pointer is the empty string.
std::wstring utf8ToWide(const char* utf8);
std::string wideToUtf8(const std::wstring& wide);

// FUN_180090960 + FUN_18008e9d0: '/' becomes '\', trailing '\' and spaces are trimmed repeatedly, ASCII letters are lower-cased (tolower in the
// "C" locale: bytes >= 0x80 unchanged; the folding follows the CRT locale state statically linked into the original DLL; a writer to it inside the DLL was not searched for, and another module's setlocale is not shown to affect it), then the name is split on
// '\' with empty components skipped and every component is hashed with FNV-1a folded by `h ^ (h >> 32)`. An empty (but non-null) name is one empty
// component (hash 0xcbf29ce44fd0bfc1); a null name gives an empty vector.
std::vector<std::uint64_t> archiveComponentHashes(const char* name);

// Installs the narrow table at object+0x20 (+0x28 vbptr). Windows or Archive placement only (std::invalid_argument otherwise).
// Destroy the binding BEFORE the adapter object is destroyed (it clears the subobject and frees its state).
struct BindingState;   // defined in the .cpp

class NarrowBinding {
public:
    NarrowBinding(void* object, wide_abi::Placement placement, Mode mode, const Hooks& hooks);
    NarrowBinding(const NarrowBinding&) = delete;
    NarrowBinding& operator=(const NarrowBinding&) = delete;
    ~NarrowBinding();

    void* narrow() const { return narrow_; }   // object + 0x20

private:
    void* object_;
    void* narrow_;
    void* base_;
    BindingState* state_;
};

inline const wide_abi::NarrowVtbl* narrowVtbl(void* narrow) { return *static_cast<const wide_abi::NarrowVtbl* const*>(narrow); }
// Wide subobject the narrow thunks call: this - 0x10.
inline void* wideFromNarrow(void* narrow) { return static_cast<char*>(narrow) - 0x10; }

} // namespace cm3d2::internal::narrow_abi
