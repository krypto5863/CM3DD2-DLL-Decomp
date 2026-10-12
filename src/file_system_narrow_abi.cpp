#include "file_system_narrow_abi.h"

#include "msvc_utf8_facet.h"

#include <cstring>
#include <new>

namespace cm3d2::internal::narrow_abi {

struct BindingState {
    Mode mode;
    Hooks hooks;
};

namespace {

constexpr std::ptrdiff_t kStateFromBase = -0x68;   // wide adapter uses base-0x60 (state) / -0x50 (flag); this one is free

// vbtable {-8, offset of the virtual base from (narrow + 8)}: same shape as the original's.
const std::int32_t kWindowsNarrowVbtable[2] = {-8, static_cast<std::int32_t>(0x1e0 - (0x20 + 8))};   // 0x1b8
const std::int32_t kArchiveNarrowVbtable[2] = {-8, static_cast<std::int32_t>(0x110 - (0x20 + 8))};    // 0xe8

void* baseFromNarrow(void* narrow) {
    char* subobject = static_cast<char*>(narrow);
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(subobject + 8);
    return subobject + 8 + vbtable[1];
}

BindingState* stateOf(void* narrow) {
    return *reinterpret_cast<BindingState**>(static_cast<char*>(baseFromNarrow(narrow)) + kStateFromBase);
}

// ---- slots ----------------------------------------------------------------------------------------------------

IFile* narrowOpen(void* self, const char* name) {
    const BindingState* state = stateOf(self);
    if (state->mode == Mode::ArchiveHashes) {
        const auto components = archiveComponentHashes(name);
        if (components.empty() || state->hooks.open_hashes == nullptr) {
            return nullptr;
        }
        return state->hooks.open_hashes(state->hooks.user, components);
    }
    const std::wstring wide = utf8ToWide(name);   // throws on invalid UTF-8, like the original
    void* wide_self = wideFromNarrow(self);
    return wide_abi::wideVtbl(wide_self)->open(wide_self, wide.c_str());
}

bool narrowExists(void* self, const char* name) {
    const BindingState* state = stateOf(self);
    if (state->mode == Mode::ArchiveHashes) {
        const auto components = archiveComponentHashes(name);
        if (components.empty() || state->hooks.exists_hashes == nullptr) {
            return false;
        }
        return state->hooks.exists_hashes(state->hooks.user, components);
    }
    const std::wstring wide = utf8ToWide(name);
    void* wide_self = wideFromNarrow(self);
    return wide_abi::wideVtbl(wide_self)->exists(wide_self, wide.c_str());
}

std::vector<std::string>* narrowList(void* self, std::vector<std::string>* out, const char* name, int mode) {
    const std::wstring wide_name = utf8ToWide(name);
    void* wide_self = wideFromNarrow(self);
    // The wide call fills a local; `out` is constructed only afterwards (the original zero-fills the caller's storage
    // after the wide call returns), so a throw while converting leaves the caller's storage untouched.
    alignas(std::vector<std::wstring>) unsigned char storage[sizeof(std::vector<std::wstring>)];
    auto* wide_list = wide_abi::wideVtbl(wide_self)->list(wide_self, reinterpret_cast<std::vector<std::wstring>*>(storage), wide_name.c_str(), mode);
    struct Guard {
        std::vector<std::wstring>* list;
        ~Guard() { list->~vector(); }
    } guard{wide_list};
    std::vector<std::string> converted;
    converted.reserve(wide_list->size());
    for (const auto& entry : *wide_list) {
        converted.push_back(wideToUtf8(entry));
    }
    new (out) std::vector<std::string>(std::move(converted));
    return out;
}

const wide_abi::NarrowVtbl kNarrowVtbl{narrowOpen, narrowExists, narrowList};

} // namespace

std::wstring utf8ToWide(const char* utf8) { return msvc_utf8::toWide(utf8); }

std::string wideToUtf8(const std::wstring& wide) { return msvc_utf8::toUtf8(wide); }

std::vector<std::uint64_t> archiveComponentHashes(const char* name) {
    std::vector<std::uint64_t> hashes;
    if (name == nullptr) {
        return hashes;
    }
    // FUN_180090960: '/' -> '\', then trailing '\' and ' ' are removed repeatedly, then every byte goes through tolower()
    // (FUN_180168b00); FUN_18008e9d0 lower-cases once more with _tolower_l(c, 0) before splitting. In the "C" locale (the default;
    // the DLL never calls setlocale) tolower() only folds ASCII A-Z, so bytes >= 0x80 are untouched.
    std::string text(name);
    for (char& c : text) {
        if (c == '/') {
            c = '\\';
        }
    }
    while (!text.empty() && (text.back() == '\\' || text.back() == ' ')) {
        text.pop_back();
    }
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    const auto hashOf = [](const char* data, std::size_t size) {
        if (size == 0) {
            return std::uint64_t{0xcbf29ce44fd0bfc1ULL};
        }
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (std::size_t i = 0; i < size; ++i) {
            h = (h ^ static_cast<unsigned char>(data[i])) * 0x100000001b3ULL;
        }
        return (h >> 32) ^ h;
    };
    std::size_t start = 0;
    for (;;) {
        const std::size_t cut = text.find_first_of("\\/", start);
        if (cut == std::string::npos) {
            hashes.push_back(hashOf(text.data() + start, text.size() - start));   // the last component, empty only for an empty name
            return hashes;
        }
        if (cut != start) {
            hashes.push_back(hashOf(text.data() + start, cut - start));            // interior / leading empty components are skipped
        }
        start = cut + 1;
    }
}

NarrowBinding::NarrowBinding(void* object, wide_abi::Placement placement, Mode mode, const Hooks& hooks)
    : object_(object), narrow_(nullptr), base_(nullptr), state_(nullptr) {
    const wide_abi::Layout* layout = nullptr;
    const std::int32_t* vbtable = nullptr;
    if (placement == wide_abi::Placement::Windows) {
        layout = &wide_abi::kWindowsLayout;
        vbtable = kWindowsNarrowVbtable;
    } else if (placement == wide_abi::Placement::Archive) {
        layout = &wide_abi::kArchiveLayout;
        vbtable = kArchiveNarrowVbtable;
    } else {
        throw std::invalid_argument("the Delegate object has no narrow interface");
    }
    state_ = new BindingState{mode, hooks};
    char* base = static_cast<char*>(object) + layout->base_offset;
    char* narrow = static_cast<char*>(object) + layout->narrow_offset;
    *reinterpret_cast<const wide_abi::NarrowVtbl**>(narrow) = &kNarrowVtbl;
    *reinterpret_cast<const std::int32_t**>(narrow + 8) = vbtable;
    *reinterpret_cast<BindingState**>(base + kStateFromBase) = state_;
    narrow_ = narrow;
    base_ = base;
}

NarrowBinding::~NarrowBinding() {
    if (narrow_ != nullptr) {
        std::memset(narrow_, 0, 16);   // vptr + vbptr
        *reinterpret_cast<BindingState**>(static_cast<char*>(base_) + kStateFromBase) = nullptr;
    }
    delete state_;
}

} // namespace cm3d2::internal::narrow_abi
