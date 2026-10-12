#include "enum_data_internal.h"

#include <memory>
#include <utility>

extern "C" {
CM3D2_EnumDataHandle CM3D2_CALL DLL_EnumData_CreateEnumData(void) {
    return new cm3d2::internal::EnumData;
}

void CM3D2_CALL DLL_EnumData_DeleteEnumData(CM3D2_EnumDataHandle data) {
    delete static_cast<cm3d2::internal::EnumData*>(data);
}

uint32_t CM3D2_CALL DLL_EnumData_AddMember(
    CM3D2_EnumDataHandle data, int32_t number, const char* text) {
    if (data == nullptr || text == nullptr) return 0;
    auto& maps = *static_cast<cm3d2::internal::EnumData*>(data);
    if (maps.by_number.count(number) || maps.by_text.count(text)) return 0;
    // Exceptions must not cross the C boundary: allocation failure reports 0
    // and rolls back a half-applied insert so the two indexes stay in step.
    try {
        auto inserted = maps.by_text.emplace(text, number);
        try {
            maps.by_number.emplace(number, text);
        } catch (...) {
            maps.by_text.erase(inserted.first);
            return 0;
        }
    } catch (...) {
        return 0;
    }
    return 1;
}

uint32_t CM3D2_CALL DLL_EnumData_RemoveMenberAtText(
    CM3D2_EnumDataHandle data, const char* text) {
    if (data == nullptr || text == nullptr) return 0;
    auto& maps = *static_cast<cm3d2::internal::EnumData*>(data);
    const auto found = maps.by_text.find(text);
    if (found == maps.by_text.end()) return 0;
    maps.by_number.erase(found->second);
    maps.by_text.erase(found);
    return 1;
}

uint32_t CM3D2_CALL DLL_EnumData_RemoveMenberAtNumber(
    CM3D2_EnumDataHandle data, int32_t number) {
    if (data == nullptr) return 0;
    auto& maps = *static_cast<cm3d2::internal::EnumData*>(data);
    const auto found = maps.by_number.find(number);
    if (found == maps.by_number.end()) return 0;
    maps.by_text.erase(found->second);
    maps.by_number.erase(found);
    return 1;
}
}
