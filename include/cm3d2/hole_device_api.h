#pragma once
#include "cm3d2/file_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The handle is a pointer to the device object, passed by address like the other handles. */
typedef void* CM3D2_HoleDeviceHandle;

CM3D2_API void CM3D2_CALL DLL_HoleDevice_Create(CM3D2_HoleDeviceHandle* out_device);
CM3D2_API void CM3D2_CALL DLL_HoleDevice_Delete(CM3D2_HoleDeviceHandle* device);
CM3D2_API void CM3D2_CALL DLL_HoleDevice_CheckHidDevice(CM3D2_HoleDeviceHandle* device);
CM3D2_API void CM3D2_CALL DLL_HoleDevice_UpdateFrame(CM3D2_HoleDeviceHandle* device);
CM3D2_API uint32_t CM3D2_CALL DLL_HoleDevice_StartThread(CM3D2_HoleDeviceHandle* device);
CM3D2_API void CM3D2_CALL DLL_HoleDevice_StopThred(CM3D2_HoleDeviceHandle* device);
CM3D2_API void CM3D2_CALL DLL_HoleDevice_SetSensitivity(CM3D2_HoleDeviceHandle* device, float sensitivity);
CM3D2_API uint32_t CM3D2_CALL DLL_HoleDevice_IsEnabled(CM3D2_HoleDeviceHandle* device);
CM3D2_API float CM3D2_CALL DLL_HoleDevice_GetPressure(CM3D2_HoleDeviceHandle* device);
CM3D2_API float CM3D2_CALL DLL_HoleDevice_GetPositon(CM3D2_HoleDeviceHandle* device);
CM3D2_API float CM3D2_CALL DLL_HoleDevice_GetLaserPosition(CM3D2_HoleDeviceHandle* device);
CM3D2_API float CM3D2_CALL DLL_HoleDevice_GetSensitivity(CM3D2_HoleDeviceHandle* device);
CM3D2_API uint32_t CM3D2_CALL DLL_HoleDevice_GetFinishButton(CM3D2_HoleDeviceHandle* device);
CM3D2_API uint32_t CM3D2_CALL DLL_HoleDevice_GetStickButton(CM3D2_HoleDeviceHandle* device);
CM3D2_API uint32_t CM3D2_CALL DLL_HoleDevice_GetLeftClickButton(CM3D2_HoleDeviceHandle* device);
CM3D2_API uint32_t CM3D2_CALL DLL_HoleDevice_GetRightClickButton(CM3D2_HoleDeviceHandle* device);
CM3D2_API float CM3D2_CALL DLL_HoleDevice_GetSecondsSpeed(CM3D2_HoleDeviceHandle* device);
CM3D2_API float CM3D2_CALL DLL_HoleDevice_GetPlanePosition(CM3D2_HoleDeviceHandle* device);
CM3D2_API int32_t CM3D2_CALL DLL_HoleDevice_GetArrowUp(CM3D2_HoleDeviceHandle* device);
CM3D2_API int32_t CM3D2_CALL DLL_HoleDevice_GetArrowDown(CM3D2_HoleDeviceHandle* device);
CM3D2_API int32_t CM3D2_CALL DLL_HoleDevice_GetArrowRight(CM3D2_HoleDeviceHandle* device);
CM3D2_API int32_t CM3D2_CALL DLL_HoleDevice_GetArrowLeft(CM3D2_HoleDeviceHandle* device);

#ifdef __cplusplus
}
#endif
