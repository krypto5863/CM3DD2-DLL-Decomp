#include "cm3d2/enum_data_api.h"
#include "cm3d2/file_system_api.h"
#include "cm3d2/file_system_delegate_api.h"
#include "cm3d2/menu_api.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Expected values were read from FUN_18010fde0 / FUN_18010ed30 / FUN_18010d790 and confirmed by
// running the same synthetic menus through the original DLL (tools/differential/menu_probe.cpp).
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string lengthPrefixed(const std::string& text) {
    std::string out;
    if (text.size() < 128) {
        out.push_back(static_cast<char>(text.size()));
    } else {
        out.push_back(static_cast<char>((text.size() & 0x7f) | 0x80));
        out.push_back(static_cast<char>(text.size() >> 7));
    }
    return out + text;
}

void u32(std::string& out, std::uint32_t value) { out.append(reinterpret_cast<const char*>(&value), 4); }

using Command = std::vector<std::string>;

void writeMenu(const std::filesystem::path& folder, const std::string& name, const std::vector<Command>& commands) {
    std::string body;
    for (const auto& command : commands) {
        body.push_back(static_cast<char>(command.size()));
        for (const auto& token : command) body += lengthPrefixed(token);
    }
    std::string file = lengthPrefixed("CM3D2_MENU");
    u32(file, 1000);
    file += lengthPrefixed(name) + lengthPrefixed("item_" + name) + lengthPrefixed("cat") + lengthPrefixed("info " + name);
    u32(file, static_cast<std::uint32_t>(body.size() + 1));
    file += body;
    file.push_back('\0');
    std::ofstream(folder / std::filesystem::u8path(name), std::ios::binary) << file;
}

struct Record {
    std::string menu_name, item_info, category_text, color_set_name, icon, save_item, priority;
    int category_mpn, color_set_mpn, multi_color;
    int bo_del_only, is_man, is_collabo;
};

Record read(CM3D2_MenuDataBaseHandle* db) {
    return {DLL_MenuParse_GetMenuName(db), DLL_MenuParse_GetItemInfoText(db), DLL_MenuParse_GetCategoryMpnText(db),
        DLL_MenuParse_GetMenuNameInColorSet(db), DLL_MenuParse_GetIconS(db), DLL_MenuParse_GetSaveItem(db),
        DLL_MenuParse_GetPriorityText(db), DLL_MenuParse_GetCategoryMpn(db), DLL_MenuParse_GetColorSetMpn(db),
        DLL_MenuParse_GetMultiColorId(db), static_cast<int>(DLL_MenuParse_GetBoDelOnly(db)), static_cast<int>(DLL_MenuParse_GetIsMan(db)),
        static_cast<int>(DLL_MenuParse_GetIsCollabo(db))};
}

} // namespace

int main() try {
    const auto owned_id=std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto folder = std::filesystem::temp_directory_path() / ("cm3d2_menu_database_contract_"+owned_id);
    require(std::filesystem::create_directory(folder),"fresh owned menu folder");
    writeMenu(folder, "a_plain.menu", {{"name", "Plain Name"}, {"icon", "a.tex"}, {"category", "WEAR"}, {"setumei", u8"line1《改行》line2"}});
    writeMenu(folder, "b_color.menu", {{"name", u8"Sp　ace"}, {"icons", "b.tex"}, {"color_set", "Skirt", "SubName"},
        {"category", "skirt"}, {"priority", "50"}, {"saveitem", "x.model"}});
    writeMenu(folder, "e_zpart_rest.menu", {{"icon", "e.tex"}, {"category", "nosuchcat"}});
    writeMenu(folder, "c_tex.menu", {{"icon", "c.tex"}, {"tex", "m", "t", "f", "1", "c3"}, {"unsetitem"}, {"collabo"}});
    writeMenu(folder, "d_jp.menu", {{"icon", "d.tex"}, {u8"メニューフォルダ", "MAN"}});
    writeMenu(folder, "h_noicon.menu", {{"name", "NoIcon"}});
    writeMenu(folder, "x_zurashi_a.menu", {{"icon", "skipped.tex"}});
    writeMenu(folder, "y_mekure_b.menu", {{"icon", "skipped.tex"}});
    std::ofstream(folder / "z_badmagic.menu", std::ios::binary) << "not a menu";

    CM3D2_FileSystemHandle fs{};
    DLL_FileSystem_CreateFileSystemWindows(&fs);
    require(DLL_FileSystem_AddFolder(&fs, folder.string().c_str()) == 1, "add folder");
    void* wide = DLL_FileSystem_GetInterfaceFileSystemWidePointer(&fs);

    void* categories = DLL_EnumData_CreateEnumData();
    void* colors = DLL_EnumData_CreateEnumData();
    DLL_EnumData_AddMember(categories, 10, "wear");
    DLL_EnumData_AddMember(categories, 11, "skirt");
    DLL_EnumData_AddMember(colors, 103, "c3");

    CM3D2_MenuDataBaseHandle db = nullptr;
    DLL_MenuDataBase_New(&db, categories, colors);
    DLL_MenuDataBase_StartAnalysis(&db, wide);
    for (int i = 0; i < 1000 && !DLL_MenuDataBase_JobFinished(&db); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(DLL_MenuDataBase_JobFinished(&db) == 1, "analysis finished");
    // a, b, e, c, d: icon-less, skipped (_zurashi/_mekure) and undecodable menus produce no record.
    require(DLL_MenuDataBase_Size(&db) == 5, "five records");

    std::vector<Record> records;
    for (int i = 0; i < 5; ++i) {
        DLL_MenuDataBase_SetIndex(&db, i);
        records.push_back(read(&db));
    }
    std::string order;
    for (int i = 0; i < 5; ++i) {
        DLL_MenuDataBase_SetIndex(&db, i);
        order += DLL_MenuParse_GetSrcFileName(&db);
        order += ' ';
    }
    require(order == "a_plain.menu b_color.menu e_zpart_rest.menu c_tex.menu d_jp.menu ", order.c_str());

    require(records[0].menu_name == "Plain" && records[0].category_mpn == 10 && records[0].category_text == "wear" &&
        records[0].item_info == "line1\nline2", "plain menu");
    require(records[1].menu_name == "Sp" && records[1].color_set_mpn == 11 && records[1].color_set_name == "subname" &&
        records[1].priority == "50" && records[1].save_item == "x.model", "color_set menu");
    require(records[2].category_mpn == 0 && records[2].category_text == "nosuchcat", "unknown category is stored with mpn 0");
    // FUN_18010d610 resets everything except the category text, so it leaks into later menus.
    require(records[3].category_text == "nosuchcat" && records[4].category_text == "nosuchcat", "category text carries over");
    require(records[3].multi_color == 103 && records[3].bo_del_only == 1 && records[3].is_collabo == 1, "tex/unsetitem/collabo");
    require(records[4].is_man == 1 && records[4].multi_color == -1, "menu folder man");

    // Parent name: first "_z" ... next '_' (else '.').
    DLL_MenuDataBase_SetIndex(&db, 2);
    const auto* parent = DLL_MenuDataBase_GetParentFileNameToNativeHash(&db, DLL_MenuParse_GetNativeHash(&db));
    require(parent != nullptr && std::string(parent) == "e_rest.menu", "parent name");
    require(DLL_MenuDataBase_GetFileNameToNativeHash(&db, 12345) == nullptr, "unknown hash");

    DLL_MenuDataBase_Delete(&db);
    require(db == nullptr, "deleted");

    // Valid supplementary characters (recovered facet): the text a command carries goes UTF-8 -> 16-bit wide -> UTF-8, so a 4-byte character
    // is reduced to its low 16 bits (U+1F600 -> U+F600 = EF 98 80); file names come back through to_bytes with each surrogate half as its own
    // 3-byte sequence (U+1F600 = D83D DE00 -> ED A0 BD ED B8 80). ASCII, Latin and Japanese text is unchanged.
    {
        const auto facet_id=std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto folder2 = std::filesystem::temp_directory_path() / ("cm3d2_menu_database_contract_facet_"+facet_id);
        require(std::filesystem::create_directory(folder2),"fresh owned facet folder");
        const std::string emoji = u8"😀";
        writeMenu(folder2, "k_emoji.menu", {{"name", "Emoji" + emoji + " rest"}, {"icon", "k.tex"}, {"setumei", "x" + emoji + u8"《改行》y"}});
        writeMenu(folder2, "l_text.menu", {{"name", u8"Café　tail"}, {"icon", "l.tex"}, {"setumei", u8"日本《改行》語"}});
        writeMenu(folder2, "m_" + emoji + ".menu", {{"icon", "m.tex"}});
        const std::string nul_unit="A\xf0\x90\x80\x80tail"; // U+10000 -> wide NUL
        writeMenu(folder2,"n_terminated.menu",{{"name",nul_unit},{"setumei",nul_unit},{"icon","n.tex"},
            {"category","X"+emoji}});
        CM3D2_FileSystemHandle fs2{};
        DLL_FileSystem_CreateFileSystemWindows(&fs2);
        require(DLL_FileSystem_AddFolder(&fs2, folder2.string().c_str()) == 1, "add facet folder");
        CM3D2_MenuDataBaseHandle db2 = nullptr;
        DLL_MenuDataBase_New(&db2, categories, colors);
        DLL_MenuDataBase_StartAnalysis(&db2, DLL_FileSystem_GetInterfaceFileSystemWidePointer(&fs2));
        for (int i = 0; i < 1000 && !DLL_MenuDataBase_JobFinished(&db2); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(DLL_MenuDataBase_JobFinished(&db2) == 1 && DLL_MenuDataBase_Size(&db2) == 4, "four facet menus");
        bool saw_emoji = false, saw_text = false, saw_name = false, saw_terminated=false;
        std::string seen;
        for (int i = 0; i < 4; ++i) {
            DLL_MenuDataBase_SetIndex(&db2, i);
            const std::string file = DLL_MenuParse_GetSrcFileName(&db2);
            const char* native_name = DLL_MenuDataBase_GetFileNameToNativeHash(&db2, DLL_MenuParse_GetNativeHash(&db2));
            const std::string listed = native_name != nullptr ? native_name : "";
            for (unsigned char c : listed) { char hex[4]; std::snprintf(hex, sizeof hex, "%02x", c); seen += hex; }
            seen += ' ';
            const auto record = read(&db2);
            if (file == "k_emoji.menu") {
                saw_emoji = true;
                require(record.menu_name == "Emoji\xEF\x98\x80" && record.item_info == "x\xEF\x98\x80\ny", "supplementary text keeps only the low 16 bits");
            } else if (file == "l_text.menu") {
                saw_text = true;
                require(record.menu_name == u8"Café" && record.item_info == u8"日本\n語", "Latin and Japanese text unchanged, full-width space splits");
            } else if (listed == "m_\xED\xA0\xBD\xED\xB8\x80.menu") {
                saw_name = true;
            } else if (file=="n_terminated.menu") {
                saw_terminated=true;
                require(record.menu_name=="A" && record.item_info=="A","menu output conversion stops at decoded NUL");
                require(record.category_text=="x"+emoji,"category remains raw narrow text with ASCII folding");
            }
        }
        require(saw_emoji && saw_text, "emoji and text menus present");
        require(saw_name, ("file name with a supplementary character: each surrogate half becomes its own 3-byte sequence; got " + seen).c_str());
        require(saw_terminated,"terminated conversion menu present");
        DLL_MenuDataBase_Delete(&db2);
        DLL_FileSystem_DeleteFileSystem(&fs2);
        std::filesystem::remove_all(folder2);
    }

    // Local malformed fixtures enter the exception handler but UI is suppressed
    // by CTest's CM3D2_NO_UI_DIALOGS setting. Analysis must continue to valid data.
    {
        const auto errors=std::filesystem::temp_directory_path()/("cm3d2_menu_errors_"+owned_id);
        require(std::filesystem::create_directory(errors),"fresh owned error folder");
        writeMenu(errors,"missing_name.menu",{{"name"},{"icon","bad.tex"}});
        writeMenu(errors,"missing_icon.menu",{{"icon"}});
        writeMenu(errors,"empty_name.menu",{{"name",""},{"icon","bad.tex"}});
        writeMenu(errors,"valid.menu",{{"name","Valid"},{"icon","ok.tex"}});
        CM3D2_FileSystemHandle error_fs{};DLL_FileSystem_CreateFileSystemWindows(&error_fs);
        require(DLL_FileSystem_AddFolder(&error_fs,errors.string().c_str())==1,"add error fixture root");
        CM3D2_MenuDataBaseHandle error_db=nullptr;DLL_MenuDataBase_New(&error_db,categories,colors);
        DLL_MenuDataBase_StartAnalysis(&error_db,DLL_FileSystem_GetInterfaceFileSystemWidePointer(&error_fs));
        for (int i=0;i<1000 && !DLL_MenuDataBase_JobFinished(&error_db);++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(DLL_MenuDataBase_JobFinished(&error_db) && DLL_MenuDataBase_Size(&error_db)==1,"malformed menus skipped, valid analysis continues");
        DLL_MenuDataBase_SetIndex(&error_db,0);
        require(std::string(DLL_MenuParse_GetMenuName(&error_db))=="Valid","valid menu survives errors");
        DLL_MenuDataBase_Delete(&error_db);DLL_FileSystem_DeleteFileSystem(&error_fs);
        std::filesystem::remove_all(errors); // exact freshly created fixture only
    }

    // A delegate wide interface is not an InterfaceToken. With no enumeration
    // callbacks its menu database is empty, but the facade must not miscast it.
    CM3D2_FileSystemDelegateHandle delegate = nullptr;
    DLL_FileSystemDelegate_Create(&delegate);
    DLL_MenuDataBase_New(&db, categories, colors);
    DLL_MenuDataBase_StartAnalysis(&db,
        DLL_FileSystemDelegate_GetInterfaceFileSystemWidePointer(&delegate));
    for (int i = 0; i < 1000 && !DLL_MenuDataBase_JobFinished(&db); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(DLL_MenuDataBase_JobFinished(&db) && DLL_MenuDataBase_Size(&db) == 0,
        "delegate facade should finish without miscasting the callback object");
    DLL_MenuDataBase_Delete(&db);
    DLL_FileSystemDelegate_Delete(&delegate);
    DLL_FileSystem_DeleteFileSystem(&fs);
    DLL_EnumData_DeleteEnumData(categories);
    DLL_EnumData_DeleteEnumData(colors);
    std::filesystem::remove_all(folder);
    std::puts("menu_database_contract ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAILED: %s\n", error.what());
    return 1;
}
