#include "cm3d2/menu_api.h"

#include "enum_data_internal.h"
#include "menu_database.h"

namespace {

using cm3d2::internal::MenuDatabase;
using cm3d2::internal::MenuRecord;

MenuDatabase* database(CM3D2_MenuDataBaseHandle* handle) {
    return handle == nullptr ? nullptr : static_cast<MenuDatabase*>(*handle);
}

// The getters read the record chosen by SetIndex (database +0x10); without one the original
// dereferences null.
const MenuRecord* record(CM3D2_MenuDataBaseHandle* handle) {
    const auto* db = database(handle);
    return db == nullptr ? nullptr : db->selected();
}

const char* text(CM3D2_MenuDataBaseHandle* handle, int slot) {
    const auto* r = record(handle);
    return r == nullptr ? "" : r->text(slot);
}

} // namespace

extern "C" {

void CM3D2_CALL DLL_MenuDataBase_New(CM3D2_MenuDataBaseHandle* out_database, void* categories, void* colors) {
    if (out_database != nullptr) {
        *out_database = new MenuDatabase(static_cast<const cm3d2::internal::EnumData*>(categories),
            static_cast<const cm3d2::internal::EnumData*>(colors));
    }
}

void CM3D2_CALL DLL_MenuDataBase_Delete(CM3D2_MenuDataBaseHandle* handle) {
    if (handle != nullptr) {
        delete static_cast<MenuDatabase*>(*handle);
        *handle = nullptr;
    }
}

void CM3D2_CALL DLL_MenuDataBase_StartAnalysis(CM3D2_MenuDataBaseHandle* handle, void* file_system_wide_interface) {
    if (auto* db = database(handle)) {
        db->start(file_system_wide_interface);
    }
}

uint32_t CM3D2_CALL DLL_MenuDataBase_JobFinished(CM3D2_MenuDataBaseHandle* handle) {
    const auto* db = database(handle);
    return db != nullptr && db->finished();
}

int64_t CM3D2_CALL DLL_MenuDataBase_Size(CM3D2_MenuDataBaseHandle* handle) {
    const auto* db = database(handle);
    return db == nullptr ? 0 : db->size();
}

void CM3D2_CALL DLL_MenuDataBase_SetIndex(CM3D2_MenuDataBaseHandle* handle, int32_t index) {
    if (auto* db = database(handle)) {
        db->setIndex(index);
    }
}

const char* CM3D2_CALL DLL_MenuDataBase_GetFileNameToNativeHash(CM3D2_MenuDataBaseHandle* handle, uint64_t hash) {
    const auto* db = database(handle);
    const std::string* name = db == nullptr ? nullptr : db->fileName(hash);
    return name == nullptr ? nullptr : name->c_str();
}

const char* CM3D2_CALL DLL_MenuDataBase_GetParentFileNameToNativeHash(CM3D2_MenuDataBaseHandle* handle, uint64_t hash) {
    const auto* db = database(handle);
    const std::string* name = db == nullptr ? nullptr : db->parentFileName(hash);
    return name == nullptr ? nullptr : name->c_str();
}

uint32_t CM3D2_CALL DLL_MenuParse_GetHeaderCheck(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->header_check : 0; }
int32_t CM3D2_CALL DLL_MenuParse_GetVersion(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? static_cast<int32_t>(r->version) : 0; }
uint64_t CM3D2_CALL DLL_MenuParse_GetNativeHash(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->hash : 0; }
const char* CM3D2_CALL DLL_MenuParse_GetSrcFileName(CM3D2_MenuDataBaseHandle* h) { return text(h, 0); }
const char* CM3D2_CALL DLL_MenuParse_GetItemName(CM3D2_MenuDataBaseHandle* h) { return text(h, 1); }
const char* CM3D2_CALL DLL_MenuParse_GetCategoryName(CM3D2_MenuDataBaseHandle* h) { return text(h, 2); }
const char* CM3D2_CALL DLL_MenuParse_GetInfoText(CM3D2_MenuDataBaseHandle* h) { return text(h, 3); }
const char* CM3D2_CALL DLL_MenuParse_GetMenuName(CM3D2_MenuDataBaseHandle* h) { return text(h, 4); }
const char* CM3D2_CALL DLL_MenuParse_GetItemInfoText(CM3D2_MenuDataBaseHandle* h) { return text(h, 5); }
const char* CM3D2_CALL DLL_MenuParse_GetCategoryMpnText(CM3D2_MenuDataBaseHandle* h) { return text(h, 6); }
int32_t CM3D2_CALL DLL_MenuParse_GetCategoryMpn(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->category_mpn : 0; }
int32_t CM3D2_CALL DLL_MenuParse_GetColorSetMpn(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->color_set_mpn : 0; }
const char* CM3D2_CALL DLL_MenuParse_GetMenuNameInColorSet(CM3D2_MenuDataBaseHandle* h) { return text(h, 7); }
int32_t CM3D2_CALL DLL_MenuParse_GetMultiColorId(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->multi_color_id : 0; }
const char* CM3D2_CALL DLL_MenuParse_GetIconS(CM3D2_MenuDataBaseHandle* h) { return text(h, 8); }
const char* CM3D2_CALL DLL_MenuParse_GetSaveItem(CM3D2_MenuDataBaseHandle* h) { return text(h, 9); }
uint32_t CM3D2_CALL DLL_MenuParse_GetBoDelOnly(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->bo_del_only : 0; }
const char* CM3D2_CALL DLL_MenuParse_GetPriorityText(CM3D2_MenuDataBaseHandle* h) { return text(h, 10); }
uint32_t CM3D2_CALL DLL_MenuParse_GetIsMan(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->is_man : 0; }
uint32_t CM3D2_CALL DLL_MenuParse_GetIsCollabo(CM3D2_MenuDataBaseHandle* h) { const auto* r = record(h); return r ? r->is_collabo : 0; }

} // extern "C"
