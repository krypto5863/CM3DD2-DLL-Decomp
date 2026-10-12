#include "file_system_wide_abi.h"

#include <cstring>
#include <new>
#include <memory>

namespace cm3d2::internal::wide_abi {
namespace {

constexpr std::ptrdiff_t kStateBackPointer = -0x60;   // from the virtual base; free in every layout
constexpr std::ptrdiff_t kFlagFromBase = -0x50;

struct State {
    Callbacks callbacks;
    Layout layout;
};

State* stateFromBase(void* base) {
    return *reinterpret_cast<State**>(static_cast<char*>(base) + kStateBackPointer);
}

State* stateFromWide(void* wide) { return stateFromBase(baseFromWide(wide)); }

// ---- wide slots ---------------------------------------------------------------------------------------------

IFile* wideOpen(void* self, const wchar_t* name) {
    const State* state = stateFromWide(self);
    if (state->callbacks.open == nullptr || name == nullptr) {
        return nullptr;
    }
    return state->callbacks.open(state->callbacks.user, name);
}

bool wideExists(void* self, const wchar_t* name) {
    const State* state = stateFromWide(self);
    if (state->callbacks.exists == nullptr || name == nullptr) {
        return false;
    }
    return state->callbacks.exists(state->callbacks.user, name);
}

std::vector<std::wstring>* wideList(void* self, std::vector<std::wstring>* out, const wchar_t* name, int mode) {
    const State* state = stateFromWide(self);
    // The native callee constructs the vector in caller-provided storage (the delegate stub zero-fills it).
    new (out) std::vector<std::wstring>();
    if (state->callbacks.list != nullptr) {
        state->callbacks.list(state->callbacks.user, *out, name != nullptr ? name : L"", mode);
    }
    return out;
}

void* wideGetMap(void* self) {
    const State* state = stateFromWide(self);
    return state->callbacks.get_file_map != nullptr ? state->callbacks.get_file_map(state->callbacks.user) : nullptr;
}

const WideVtbl kWideVtbl{wideOpen, wideExists, wideList, wideGetMap};

// ---- virtual base slots (this = vbase; vtordisp is 0 here, the native thunk subtracts it first) --------------

void* baseDeletingDestructor(void* self, unsigned flags) {
    State* state = stateFromBase(self);
    const Layout layout = state->layout;
    if (state->callbacks.destroy != nullptr) {
        state->callbacks.destroy(state->callbacks.user);
    }
    delete state;
    char* object = static_cast<char*>(self) - layout.base_offset;
    if ((flags & 1U) != 0) {
        ::operator delete(object);
    }
    return object;
}

bool baseIsValid(void* self) {
    const State* state = stateFromBase(self);
    return state->callbacks.is_valid == nullptr || state->callbacks.is_valid(state->callbacks.user);
}

bool baseGetFlag(void* self) {
    const State* state = stateFromBase(self);
    auto* flag = reinterpret_cast<std::uint8_t*>(static_cast<char*>(self) + kFlagFromBase);
    if (state->callbacks.get_flag != nullptr)
        *flag = state->callbacks.get_flag(state->callbacks.user) ? 1 : 0;
    return *flag != 0;
}

bool baseSetFlag(void* self, bool value) {
    const State* state = stateFromBase(self);
    if (state->callbacks.set_flag != nullptr)
        value = state->callbacks.set_flag(state->callbacks.user, value);
    *reinterpret_cast<std::uint8_t*>(static_cast<char*>(self) + kFlagFromBase) = value ? 1 : 0;
    return value;
}

void* baseGetMap(void* self) {
    const State* state = stateFromBase(self);
    return state->callbacks.get_menu_set != nullptr ? state->callbacks.get_menu_set(state->callbacks.user) : nullptr;
}

const BaseVtbl kBaseVtbl{baseDeletingDestructor, baseIsValid, baseGetFlag, baseSetFlag, baseGetMap};

// vbtables {-8, offset of the virtual base from (wide + 8)} as in the native tables.
const std::int32_t kWindowsVbtable[2] = {-8, static_cast<std::int32_t>(0x1e0 - (0x10 + 8))};   // 0x1c8
const std::int32_t kWindowsPrimaryVbtable[2] = {-8, 0x1e0 - 8};
const std::int32_t kArchivePrimaryVbtable[2] = {-8, 0x110 - 8};
const std::int32_t kArchiveVbtable[2] = {-8, static_cast<std::int32_t>(0x110 - (0x10 + 8))};
const std::int32_t kDelegateVbtable[2] = {-8, static_cast<std::int32_t>(0x120 - (0x00 + 8))};  // 0x118

} // namespace

void* baseFromWide(void* wide) {
    char* subobject = static_cast<char*>(wide);
    const auto* vbtable = *reinterpret_cast<const std::int32_t* const*>(subobject + 8);
    return subobject + 8 + vbtable[1];
}

Adapter createAdapter(Placement placement, const Callbacks& callbacks) {
    const Layout* layout = &kWindowsLayout;
    const std::int32_t* vbtable = kWindowsVbtable;
    const std::int32_t* primary = kWindowsPrimaryVbtable;
    if (placement == Placement::Archive) {
        layout = &kArchiveLayout;
        vbtable = kArchiveVbtable;
        primary = kArchivePrimaryVbtable;
    } else if (placement == Placement::Delegate) {
        layout = &kDelegateLayout;
        vbtable = kDelegateVbtable;
        primary = kDelegateVbtable;
    }

    // Both allocations remain owned until every initialization step succeeds.
    auto state = std::make_unique<State>(State{callbacks, *layout});
    std::unique_ptr<void, void(*)(void*)> allocation(
        ::operator new(layout->object_size), static_cast<void(*)(void*)>(&::operator delete));
    char* object = static_cast<char*>(allocation.get());
    std::memset(object, 0, layout->object_size);
    *reinterpret_cast<const std::int32_t**>(object + 8) = primary;

    char* wide = object + layout->wide_offset;
    char* base = object + layout->base_offset;
    *reinterpret_cast<const WideVtbl**>(wide) = &kWideVtbl;
    *reinterpret_cast<const std::int32_t**>(wide + 8) = vbtable;
    *reinterpret_cast<const BaseVtbl**>(base) = &kBaseVtbl;
    *reinterpret_cast<std::int32_t*>(base - 4) = 0;   // vtordisp

    *reinterpret_cast<State**>(base + kStateBackPointer) = state.get();
    if (callbacks.get_flag != nullptr)
        *reinterpret_cast<std::uint8_t*>(base + kFlagFromBase) = callbacks.get_flag(callbacks.user) ? 1 : 0;

    Adapter adapter;
    adapter.object = object;
    adapter.wide = wide;
    adapter.base = base;
    state.release();
    allocation.release();
    return adapter;
}

} // namespace cm3d2::internal::wide_abi
