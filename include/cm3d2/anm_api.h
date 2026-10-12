#pragma once
#include "cm3d2/file_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DLL_AnmParse_* (0x1800673b0..0x180067610). Parser handles are exchanged by
 * pointer like the file API. The list/curve/key-frame-list values returned by
 * the getters are borrowed pointers into the parser and stay valid until the
 * next Open/OpenByte/Clear/Delete.
 */
typedef void* CM3D2_AnmParseHandle;
typedef void* CM3D2_AnmCurveListHandle;
typedef void* CM3D2_AnmCurveHandle;
typedef void* CM3D2_AnmKeyFrameListHandle;

typedef struct CM3D2_AnmKeyFrame {
    float time;
    float value;
    float in_tangent;
    float out_tangent;
} CM3D2_AnmKeyFrame;

CM3D2_API void CM3D2_CALL DLL_AnmParse_New(CM3D2_AnmParseHandle* out_parser);
CM3D2_API void CM3D2_CALL DLL_AnmParse_Delete(CM3D2_AnmParseHandle* parser);

/* `interface_file` is the pointer from DLL_FileMemory_GetInterfaceFilePointer. */
CM3D2_API uint32_t CM3D2_CALL DLL_AnmParse_Open(
    CM3D2_AnmParseHandle* parser,
    void* interface_file,
    uint8_t keep_mune_l,
    uint8_t keep_mune_r,
    uint8_t keep_all_bones);
CM3D2_API uint32_t CM3D2_CALL DLL_AnmParse_OpenByte(
    CM3D2_AnmParseHandle* parser,
    const uint8_t* data,
    int32_t byte_count,
    uint8_t keep_mune_l,
    uint8_t keep_mune_r,
    uint8_t keep_all_bones);
CM3D2_API void CM3D2_CALL DLL_AnmParse_Clear(CM3D2_AnmParseHandle* parser);
CM3D2_API uint32_t CM3D2_CALL DLL_AnmParse_IsValid(CM3D2_AnmParseHandle* parser);

CM3D2_API CM3D2_AnmCurveListHandle CM3D2_CALL DLL_AnmParse_GetAnimationCurveDataList(
    CM3D2_AnmParseHandle* parser);
CM3D2_API int64_t CM3D2_CALL DLL_AnmParse_AnimationCurveDataList_Size(
    CM3D2_AnmCurveListHandle list);
CM3D2_API CM3D2_AnmCurveHandle CM3D2_CALL DLL_AnmParse_AnimationCurveDataList_At(
    CM3D2_AnmCurveListHandle list, int32_t index);

CM3D2_API const char* CM3D2_CALL DLL_AnmParse_AnimationCurveData_GetPath(
    CM3D2_AnmCurveHandle curve);
CM3D2_API uint32_t CM3D2_CALL DLL_AnmParse_AnimationCurveData_ComID(
    CM3D2_AnmCurveHandle curve);
CM3D2_API CM3D2_AnmKeyFrameListHandle CM3D2_CALL DLL_AnmParse_AnimationCurveData_KeyFrameList(
    CM3D2_AnmCurveHandle curve);

CM3D2_API int64_t CM3D2_CALL DLL_AnmParse_KeyFrameList_Size(
    CM3D2_AnmKeyFrameListHandle list);
CM3D2_API void CM3D2_CALL DLL_AnmParse_KeyFrameList_At(
    CM3D2_AnmKeyFrameListHandle list, int32_t index, CM3D2_AnmKeyFrame* out_key_frame);

#ifdef __cplusplus
}
#endif
