// Drives the narrow table through raw vptrs on top of the wide adapter (Windows and Archive placements), with fake callbacks.
#include "file_system_narrow_abi.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace cm3d2::internal;
using namespace cm3d2::internal::narrow_abi;
using cm3d2::internal::wide_abi::Callbacks;
using cm3d2::internal::wide_abi::Placement;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class OwnedFile final : public IFile {
public:
    ~OwnedFile() override { ++destroyed; }
    void close() override {}
    bool seek(std::int64_t, bool) override { return true; }
    std::uint64_t read(void*, std::uint64_t) override { return 0; }
    std::uint64_t readAt(void*, std::uint64_t, std::uint64_t) override { return 0; }
    bool valid() const override { return true; }
    const std::byte* data() const override { return nullptr; }
    std::uint64_t tell() const override { return 0; }
    std::uint64_t size() const override { return 3; }
    static int destroyed;
};
int OwnedFile::destroyed = 0;

struct Wide {
    std::wstring last_open, last_exists, last_list;
    int last_mode = -1;
    int opens = 0, exists_calls = 0, lists = 0;
};

IFile* wideOpen(void* user, const wchar_t* name) {
    auto* wide = static_cast<Wide*>(user);
    ++wide->opens;
    wide->last_open = name;
    return std::wstring(name) == L"none" ? nullptr : new OwnedFile();
}
bool wideExists(void* user, const wchar_t* name) {
    auto* wide = static_cast<Wide*>(user);
    ++wide->exists_calls;
    wide->last_exists = name;
    return std::wstring(name) == L"yes";
}
void wideList(void* user, std::vector<std::wstring>& out, const wchar_t* name, int mode) {
    auto* wide = static_cast<Wide*>(user);
    ++wide->lists;
    wide->last_list = name;
    wide->last_mode = mode;
    out.push_back(L"ascii.txt");
    out.push_back(L"café.txt");           // 2-byte UTF-8
    out.push_back(L"日本.menu");       // 3-byte UTF-8
}

struct Hashes {
    std::vector<std::uint64_t> last_open, last_exists;
    int opens = 0, exists_calls = 0;
};
IFile* hashOpen(void* user, const std::vector<std::uint64_t>& components) {
    auto* hashes = static_cast<Hashes*>(user);
    ++hashes->opens;
    hashes->last_open = components;
    return new OwnedFile();
}
bool hashExists(void* user, const std::vector<std::uint64_t>& components) {
    auto* hashes = static_cast<Hashes*>(user);
    ++hashes->exists_calls;
    hashes->last_exists = components;
    return true;
}

bool throwsBadConversion(void (*run)()) {
    try {
        run();
    } catch (const std::range_error& error) {
        return std::string(error.what()) == "bad conversion";
    }
    return false;
}

void windowsMode() {
    Wide wide;
    Callbacks callbacks;
    callbacks.user = &wide;
    callbacks.open = wideOpen;
    callbacks.exists = wideExists;
    callbacks.list = wideList;
    auto adapter = wide_abi::createAdapter(Placement::Windows, callbacks);
    {
        NarrowBinding binding(adapter.object, Placement::Windows, Mode::ForwardToWide, Hooks{});
        void* narrow = binding.narrow();
        check(narrow == static_cast<char*>(adapter.object) + 0x20, "narrow interface sits at object+0x20");
        check(wideFromNarrow(narrow) == adapter.wide, "narrow-0x10 is the wide interface");
        const auto* table = narrowVtbl(narrow);

        const int before = OwnedFile::destroyed;
        IFile* file = table->open(narrow, "caf\xc3\xa9/item.txt");
        check(file != nullptr && file->size() == 3, "open returns the wide file");
        check(wide.last_open == L"café/item.txt", "UTF-8 name converted to wide");
        delete file;
        check(OwnedFile::destroyed == before + 1, "caller owns the file");
        check(table->open(narrow, "none") == nullptr, "wide open null -> null");
        IFile* empty_name = table->open(narrow, nullptr);
        check(wide.last_open.empty(), "null name = empty wide name (the wide hook is still called)");
        delete empty_name;

        check(table->exists(narrow, "yes") && !table->exists(narrow, "no"), "exists forwards");
        check(wide.last_exists == L"no", "exists name converted");

        // list: converted back to UTF-8, out constructed after the wide call, returned
        alignas(std::vector<std::string>) unsigned char raw[sizeof(std::vector<std::string>)];
        std::memset(raw, 0xCD, sizeof raw);
        auto* out = reinterpret_cast<std::vector<std::string>*>(raw);
        check(table->list(narrow, out, "folder\\sub", 3) == out, "list returns out");
        check(out->size() == 3 && (*out)[0] == "ascii.txt" && (*out)[1] == "caf\xc3\xa9.txt" && (*out)[2] == "\xe6\x97\xa5\xe6\x9c\xac.menu", "list converted to UTF-8");
        check(wide.last_list == L"folder\\sub" && wide.last_mode == 3, "list forwards name and mode");
        out->~vector();
        check(table->list(narrow, out, nullptr, 0) == out && wide.last_list.empty(), "null list name = empty");
        out->~vector();

        // Invalid UTF-8 throws exactly like the original's wstring_convert; nothing is constructed in `out`.
        check(throwsBadConversion([] {}) == false, "helper sanity");
        bool threw = false;
        try { table->open(narrow, "\xc3"); } catch (const std::range_error&) { threw = true; }
        check(threw, "truncated sequence throws");
        threw = false;
        try { table->exists(narrow, "\x80"); } catch (const std::range_error&) { threw = true; }
        check(threw, "stray continuation byte throws");
        threw = false;
        std::memset(raw, 0xCD, sizeof raw);
        try { table->list(narrow, out, "\xc3", 0); } catch (const std::range_error&) { threw = true; }
        check(threw && raw[0] == 0xCD, "bad list name throws before touching out");

        check(narrowVtbl(narrow) != nullptr, "table installed");
    }
    // binding destroyed: the subobject is cleared
    check(*reinterpret_cast<void**>(static_cast<char*>(adapter.object) + 0x20) == nullptr, "binding clears the narrow vptr");
    wide_abi::baseVtbl(adapter.base)->deleting_destructor(adapter.base, 1);
}

void archiveMode() {
    Wide wide;
    Hashes hashes;
    Callbacks callbacks;
    callbacks.user = &wide;
    callbacks.list = wideList;
    auto adapter = wide_abi::createAdapter(Placement::Archive, callbacks);
    Hooks hooks;
    hooks.user = &hashes;
    hooks.open_hashes = hashOpen;
    hooks.exists_hashes = hashExists;
    {
        NarrowBinding binding(adapter.object, Placement::Archive, Mode::ArchiveHashes, hooks);
        void* narrow = binding.narrow();
        const auto* table = narrowVtbl(narrow);

        // Independently computed FNV-1a (folded) of the lower-cased components.
        const std::uint64_t a = 0xaf63dc4c296230c0ULL, menu = 0x42ff01a2a1eeef38ULL, a_menu = 0x206161ecadddf4b5ULL, readme = 0xe0ef565c190fde7dULL;
        check(archiveComponentHashes("a") == std::vector<std::uint64_t>({a}), "single component");
        check(archiveComponentHashes("MENU\\A.Menu") == std::vector<std::uint64_t>({menu, a_menu}), "case folded, backslash split");
        check(archiveComponentHashes("menu/a.menu") == std::vector<std::uint64_t>({menu, a_menu}), "forward slash split");
        check(archiveComponentHashes("menu\\\\a") == std::vector<std::uint64_t>({menu, a}), "interior empty component skipped");
        check(archiveComponentHashes("menu\\") == std::vector<std::uint64_t>({menu}), "a trailing separator is trimmed by the normaliser");
        check(archiveComponentHashes("MENU/ \\ ") == std::vector<std::uint64_t>({menu}), "trailing separators and spaces are trimmed repeatedly");
        check(archiveComponentHashes("a\\ b") == archiveComponentHashes("A\\ B") && archiveComponentHashes("a\\ b").size() == 2, "interior spaces stay");
        check(archiveComponentHashes("  ") == std::vector<std::uint64_t>({0xcbf29ce44fd0bfc1ULL}) &&
              archiveComponentHashes("\\") == std::vector<std::uint64_t>({0xcbf29ce44fd0bfc1ULL}), "a name that trims to nothing is one empty component");
        check(archiveComponentHashes("\xc9") == archiveComponentHashes("\xc9") && archiveComponentHashes("\xc9") != archiveComponentHashes("\xe9"),
              "bytes >= 0x80 are not case folded (C locale)");
        check(archiveComponentHashes("") == std::vector<std::uint64_t>({0xcbf29ce44fd0bfc1ULL}), "empty string is one empty component");
        check(archiveComponentHashes(nullptr).empty(), "null name -> no components");
        check(archiveComponentHashes("readme.txt") == std::vector<std::uint64_t>({readme}), "plain file name");

        const int before = OwnedFile::destroyed;
        IFile* file = table->open(narrow, "Menu\\A.menu");
        check(file != nullptr && hashes.last_open == std::vector<std::uint64_t>({menu, a_menu}), "open passes the component hashes");
        delete file;
        check(OwnedFile::destroyed == before + 1, "caller owns the archive file");
        check(table->open(narrow, nullptr) == nullptr && hashes.opens == 1, "null name -> null without reaching the hook");
        check(table->exists(narrow, "readme.txt") && hashes.last_exists == std::vector<std::uint64_t>({readme}), "exists passes hashes");
        check(!table->exists(narrow, nullptr) && hashes.exists_calls == 1, "null name -> false");
        // Non-UTF-8 bytes are fine here: the archive path hashes raw bytes, it never converts.
        check(table->open(narrow, "\xff\xfe") != nullptr, "archive open does not validate UTF-8");

        // list shares the Windows implementation: it goes through the wide table and converts.
        alignas(std::vector<std::string>) unsigned char raw[sizeof(std::vector<std::string>)];
        auto* out = reinterpret_cast<std::vector<std::string>*>(raw);
        check(table->list(narrow, out, "x", 3) == out && out->size() == 3 && wide.last_mode == 3, "archive list forwards through wide slot 2");
        out->~vector();
    }
    wide_abi::baseVtbl(adapter.base)->deleting_destructor(adapter.base, 1);

    bool threw = false;
    try {
        auto delegate = wide_abi::createAdapter(Placement::Delegate, Callbacks{});
        try { NarrowBinding bad(delegate.object, Placement::Delegate, Mode::ForwardToWide, Hooks{}); } catch (const std::invalid_argument&) { threw = true; }
        wide_abi::baseVtbl(delegate.base)->deleting_destructor(delegate.base, 1);
    } catch (...) {
    }
    check(threw, "the Delegate object has no narrow interface");
}

void conversions() {
    check(utf8ToWide("") .empty() && utf8ToWide(nullptr).empty(), "empty and null");
    check(utf8ToWide("A\xc3\xa9\xe6\x97\xa5") == L"Aé日", "1/2/3-byte sequences");
    check(wideToUtf8(L"Aé日") == "A\xc3\xa9\xe6\x97\xa5", "round trip");
    for (const char* bad : {"\xc3", "\xe6\x97", "\x80", "\xc3\x28"}) {
        bool threw = false;
        try { utf8ToWide(bad); } catch (const std::range_error&) { threw = true; }
        check(threw, "invalid UTF-8 rejected");
    }
    // The facet's finer rules (overlongs, 4-byte forms, surrogates) are covered by file_system_narrow_conversion_contract.
    check(wideToUtf8(std::wstring(1, static_cast<wchar_t>(0xd800))) == "\xed\xa0\x80", "a lone surrogate unit is encoded, never rejected");
}

} // namespace

int main() {
    conversions();
    windowsMode();
    archiveMode();
    std::cout << "file_system_narrow_abi_contract: ok\n";
    return 0;
}
