// Calls the adapter's slots the way the original's consumers do: through the raw vptr tables, locating the
// virtual base with *(int*)(*(wide+8)+4) + wide + 8. Fake callbacks only; no game files.
#include "file_system_wide_abi.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

namespace {

using namespace cm3d2::internal;
using namespace cm3d2::internal::wide_abi;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class BytesFile final : public IFile {
public:
    explicit BytesFile(std::string bytes) : bytes_(std::move(bytes)) {}
    ~BytesFile() override { ++destroyed; }
    void close() override {}
    bool seek(std::int64_t, bool) override { return true; }
    std::uint64_t read(void*, std::uint64_t) override { return 0; }
    std::uint64_t readAt(void* destination, std::uint64_t offset, std::uint64_t count) override {
        if (offset >= bytes_.size()) return 0;
        const std::uint64_t n = std::min<std::uint64_t>(count, bytes_.size() - offset);
        std::memcpy(destination, bytes_.data() + offset, static_cast<std::size_t>(n));
        return n;
    }
    bool valid() const override { return true; }
    const std::byte* data() const override { return reinterpret_cast<const std::byte*>(bytes_.data()); }
    std::uint64_t tell() const override { return 0; }
    std::uint64_t size() const override { return bytes_.size(); }
    static int destroyed;
private:
    std::string bytes_;
};
int BytesFile::destroyed = 0;

struct Probe {
    int opens = 0, exists_calls = 0, lists = 0, destroys = 0;
    int last_mode = -1;
    std::wstring last_name;
    bool valid = true;
    bool flag = true;
    int flag_sets = 0;
};

IFile* onOpen(void* user, const wchar_t* name) {
    auto* probe = static_cast<Probe*>(user);
    ++probe->opens;
    probe->last_name = name;
    if (std::wstring(name) == L"missing") return nullptr;
    return new BytesFile("payload");
}
bool onExists(void* user, const wchar_t* name) {
    auto* probe = static_cast<Probe*>(user);
    ++probe->exists_calls;
    return std::wstring(name) == L"present";
}
void onList(void* user, std::vector<std::wstring>& out, const wchar_t* name, int mode) {
    auto* probe = static_cast<Probe*>(user);
    ++probe->lists;
    probe->last_mode = mode;
    probe->last_name = name;
    out.push_back(L"a");
    out.push_back(L"b");
}
bool onValid(void* user) { return static_cast<Probe*>(user)->valid; }
void onDestroy(void* user) { ++static_cast<Probe*>(user)->destroys; }
void* onFileMap(void*) { static int marker; return &marker; }
void* onMenuSet(void*) { static int marker; return &marker; }
bool onGetFlag(void* user) { return static_cast<Probe*>(user)->flag; }
bool onSetFlag(void* user, bool value) {
    auto* probe=static_cast<Probe*>(user);
    ++probe->flag_sets;
    probe->flag=value;
    return value;
}
bool throwingFlag(void*) { throw std::runtime_error("flag initialization"); }

void exercise(Placement placement, const Layout& layout, bool wide_at_zero) {
    Probe probe;
    Callbacks callbacks;
    callbacks.user = &probe;
    callbacks.open = onOpen;
    callbacks.exists = onExists;
    callbacks.list = onList;
    callbacks.is_valid = onValid;
    callbacks.destroy = onDestroy;
    Adapter adapter = createAdapter(placement, callbacks);

    check(adapter.wide == static_cast<char*>(adapter.object) + layout.wide_offset, "wide placement");
    check(adapter.base == static_cast<char*>(adapter.object) + layout.base_offset, "base placement");
    check(wide_at_zero == (adapter.wide == adapter.object), "delegate wide interface sits at +0");
    check(baseFromWide(adapter.wide) == adapter.base, "base located through the wide vbptr");

    const WideVtbl* wide_table = wideVtbl(adapter.wide);
    const BaseVtbl* base_table = baseVtbl(adapter.base);

    // slot 0 open: caller owns the result; null name / hook failure -> null
    const int destroyed_before = BytesFile::destroyed;
    IFile* file = wide_table->open(adapter.wide, L"hello");
    check(file != nullptr && file->size() == 7, "open returns the hook's file");
    check(probe.opens == 1 && probe.last_name == L"hello", "open forwarded the name");
    delete file;
    check(BytesFile::destroyed == destroyed_before + 1, "caller deletes the opened file");
    check(wide_table->open(adapter.wide, L"missing") == nullptr, "open hook null -> null");
    check(wide_table->open(adapter.wide, nullptr) == nullptr, "null name -> null without calling the hook");
    check(probe.opens == 2, "null name does not reach the hook");

    // slot 1 exists
    check(wide_table->exists(adapter.wide, L"present") && !wide_table->exists(adapter.wide, L"absent"), "exists");
    check(!wide_table->exists(adapter.wide, nullptr), "exists(null)");

    // slot 2 list: the callee constructs the vector in raw storage and returns it
    alignas(std::vector<std::wstring>) unsigned char storage[sizeof(std::vector<std::wstring>)];
    std::memset(storage, 0xCD, sizeof storage);
    auto* out = reinterpret_cast<std::vector<std::wstring>*>(storage);
    auto* returned = wide_table->list(adapter.wide, out, L"folder", 3);
    check(returned == out, "list returns out");
    check(out->size() == 2 && (*out)[0] == L"a" && (*out)[1] == L"b", "list contents");
    check(probe.lists == 1 && probe.last_mode == 3 && probe.last_name == L"folder", "list forwarded name and mode");
    out->~vector();

    // slot 2 without hook -> empty vector (delegate stub behaviour)
    {
        Callbacks bare;
        Adapter plain = createAdapter(placement, bare);
        alignas(std::vector<std::wstring>) unsigned char raw[sizeof(std::vector<std::wstring>)];
        std::memset(raw, 0xCD, sizeof raw);
        auto* empty = reinterpret_cast<std::vector<std::wstring>*>(raw);
        check(wideVtbl(plain.wide)->list(plain.wide, empty, L"", 0) == empty && empty->empty(), "no hook -> empty list");
        empty->~vector();
        check(wideVtbl(plain.wide)->open(plain.wide, L"x") == nullptr, "no hook -> open null");
        check(!wideVtbl(plain.wide)->exists(plain.wide, L"x"), "no hook -> exists false");
        check(wideVtbl(plain.wide)->get_map(plain.wide) == nullptr, "map stays pending without a hook");
        check(baseVtbl(plain.base)->is_valid(plain.base), "default validity is true");
        baseVtbl(plain.base)->deleting_destructor(plain.base, 1);
    }

    // virtual base: validity, mode flag getter/setter at base-0x50, map hook
    check(base_table->is_valid(adapter.base), "valid");
    probe.valid = false;
    check(!base_table->is_valid(adapter.base), "validity follows the hook");
    check(!base_table->get_flag(adapter.base), "flag starts clear");
    check(base_table->set_flag(adapter.base, true) && base_table->get_flag(adapter.base), "set_flag(true)");
    check(*(static_cast<unsigned char*>(adapter.base) - 0x50) == 1, "flag byte lives at base-0x50");
    check(!base_table->set_flag(adapter.base, false) && !base_table->get_flag(adapter.base), "set_flag(false)");
    check(base_table->get_map(adapter.base) == nullptr, "map pending without a hook");

    // deleting destructor: runs the destroy hook, returns the complete object, flags&1 frees it
    void* complete = base_table->deleting_destructor(adapter.base, 1);
    check(complete == adapter.object, "deleting destructor returns the complete object");
    check(probe.destroys == 1, "destroy hook ran once");
}

} // namespace

int main() {
    exercise(Placement::Windows, kWindowsLayout, false);
    exercise(Placement::Archive, kArchiveLayout, false);
    exercise(Placement::Delegate, kDelegateLayout, true);

    // map hook is forwarded verbatim (pending ABI: no native map is synthesised)
    Callbacks with_map;
    with_map.get_file_map = onFileMap;
    with_map.get_menu_set = onMenuSet;
    Adapter adapter = createAdapter(Placement::Delegate, with_map);
    check(wideVtbl(adapter.wide)->get_map(adapter.wide) != nullptr, "wide get_map forwards the hook");
    check(baseVtbl(adapter.base)->get_map(adapter.base) != nullptr, "base get_map forwards the hook");
    check(wideVtbl(adapter.wide)->get_map(adapter.wide) != baseVtbl(adapter.base)->get_map(adapter.base),
        "file map and menu set have distinct hooks");
    baseVtbl(adapter.base)->deleting_destructor(adapter.base, 1);

    for (const auto placement : {Placement::Windows,Placement::Archive,Placement::Delegate}) {
        Probe backend;
        Callbacks mode;
        mode.user=&backend; mode.get_flag=onGetFlag; mode.set_flag=onSetFlag;
        auto linked=createAdapter(placement,mode);
        const auto* table=baseVtbl(linked.base);
        check(*(static_cast<unsigned char*>(linked.base)-0x50)==1,"initial backend flag mirrored");
        backend.flag=false;
        check(!table->get_flag(linked.base),"getter follows backend changes");
        check(table->set_flag(linked.base,true) && backend.flag && backend.flag_sets==1,
            "setter changes backend mode");
        check(*(static_cast<unsigned char*>(linked.base)-0x50)==1,"setter mirrors native-offset flag");
        check(!table->set_flag(linked.base,false) && !backend.flag && backend.flag_sets==2,
            "clear changes backend mode");
        // flags=0 destroys the state but leaves complete-object storage to its owner.
        check(table->deleting_destructor(linked.base,0)==linked.object,"non-freeing destructor returns object");
        ::operator delete(linked.object);
    }
    Callbacks failed;
    failed.get_flag=throwingFlag;
    failed.user=nullptr;
    bool caught=false;
    try { createAdapter(Placement::Windows,failed); }
    catch (const std::runtime_error&) { caught=true; }
    check(caught,"initialization hook exceptions propagate");

    std::cout << "file_system_wide_abi_contract: ok\n";
    return 0;
}
