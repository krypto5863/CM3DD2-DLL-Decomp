#pragma once
#include "cm3d2/file_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* CM3D2_EnumDataHandle;
CM3D2_API CM3D2_EnumDataHandle CM3D2_CALL DLL_EnumData_CreateEnumData(void);
CM3D2_API void CM3D2_CALL DLL_EnumData_DeleteEnumData(CM3D2_EnumDataHandle data);
CM3D2_API uint32_t CM3D2_CALL DLL_EnumData_AddMember(
    CM3D2_EnumDataHandle data, int32_t number, const char* text);
CM3D2_API uint32_t CM3D2_CALL DLL_EnumData_RemoveMenberAtText(
    CM3D2_EnumDataHandle data, const char* text);
CM3D2_API uint32_t CM3D2_CALL DLL_EnumData_RemoveMenberAtNumber(
    CM3D2_EnumDataHandle data, int32_t number);

#ifdef __cplusplus
}
#endif
