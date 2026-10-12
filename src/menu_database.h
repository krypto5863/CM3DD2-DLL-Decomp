#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "menu_format.h"

namespace cm3d2::internal {

struct EnumData;

// What FUN_18010d790 leaves in its context for one menu file (field names follow the getters).
struct MenuFields {
    bool header_check = false;
    std::uint32_t command_bytes = 0;  // record +0x14
    std::uint32_t version = 0;
    std::int32_t category_mpn = 0;
    std::int32_t color_set_mpn = 0;
    std::int32_t multi_color_id = -1;
    bool bo_del_only = false;
    bool is_man = false;
    bool is_collabo = false;
    std::string source_file;
    std::string item_name;
    std::string category_name;
    std::string info_text;
    std::string menu_name;
    std::string item_info_text;
    std::string category_mpn_text;
    std::string menu_name_in_color_set;
    std::string icon_s;
    std::string save_item;
    std::string priority_text;
};

// FUN_18010d790 after the carrier is read: the command interpreter. Missing
// required tokens/empty name split throw "invalid vector subscript" into the
// loader's std::exception handler. Exception RTTI is an implementation substitute.
bool interpretMenuCommands(const MenuDocument& document, const EnumData& categories,
    const EnumData& colors, MenuFields& fields);

// The packed record FUN_18010ed30 builds: a 0x48-byte header followed by eleven NUL-terminated
// strings. The getters return offsets into that pool.
struct MenuRecord {
    std::uint64_t hash = 0;
    std::uint8_t header_check = 0;
    std::uint32_t command_bytes = 0;
    std::uint32_t version = 0;
    std::int32_t category_mpn = 0;
    std::int32_t color_set_mpn = 0;
    std::int32_t multi_color_id = -1;
    std::uint8_t bo_del_only = 0;
    std::uint8_t is_man = 0;
    std::uint8_t is_collabo = 0;
    std::uint16_t offsets[11] = {};
    std::string pool;

    const char* text(int slot) const { return pool.c_str() + offsets[slot]; }
};

// DLL_MenuDataBase (constructor FUN_180072330).
class MenuDatabase {
public:
    MenuDatabase(const EnumData* categories, const EnumData* colors) : categories_(categories), colors_(colors) {}
    ~MenuDatabase();

    // FUN_18010f720: runs the analysis on a worker thread. `wide_interface` is the pointer
    // DLL_FileSystem_GetInterfaceFileSystemWidePointer returns.
    void start(const void* wide_interface);
    bool finished() const { return finished_.load(); }
    std::int64_t size() const { return static_cast<std::int64_t>(records_.size()); }
    void setIndex(std::int32_t index);
    const MenuRecord* selected() const { return selected_; }
    const std::string* fileName(std::uint64_t hash) const;
    const std::string* parentFileName(std::uint64_t hash) const;

private:
    struct Names {
        std::string parent;
        std::string file_name;
    };

    void run(const void* wide_interface);
    void load(const void* wide_interface, const std::wstring& name);

    const EnumData* categories_;
    const EnumData* colors_;
    std::atomic<bool> finished_{false};
    std::atomic<bool> cancelled_{false};
    bool started_ = false;
    std::thread worker_;
    std::vector<std::unique_ptr<MenuRecord>> records_;
    std::unordered_map<std::uint64_t, Names> names_;
    const MenuRecord* selected_ = nullptr;
    MenuFields interpreter_state_;  // shared across menus like the original worker (see interpretMenuCommands)
};

} // namespace cm3d2::internal
