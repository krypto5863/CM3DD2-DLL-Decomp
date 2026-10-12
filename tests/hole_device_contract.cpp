#include "cm3d2/hole_device_api.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <limits>

// State-only checks read from the getters in 0x18006ea20..0x18006eeb0. The device I/O paths (HID search,
// reader thread filters against real reports, DirectInput polling) need the hardware and are UNVERIFIED.
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() try {
    CM3D2_HoleDeviceHandle device = nullptr;
    DLL_HoleDevice_Create(&device);
    require(device != nullptr, "created");

    // Fresh object: everything is zero and the input is not enabled.
    require(DLL_HoleDevice_IsEnabled(&device) == 0, "not enabled");
    require(DLL_HoleDevice_GetPressure(&device) == 0.0f, "pressure");
    require(DLL_HoleDevice_GetSensitivity(&device) == 0.0f, "sensitivity starts at 0");
    require(DLL_HoleDevice_GetPlanePosition(&device) == 0.0f, "plane");
    require(DLL_HoleDevice_GetSecondsSpeed(&device) == 0.0f, "speed");
    require(DLL_HoleDevice_GetPositon(&device) == 0.0f, "position");
    require(DLL_HoleDevice_GetLaserPosition(&device) == 0.0f, "laser");
    require(DLL_HoleDevice_GetArrowUp(&device) == 0 && DLL_HoleDevice_GetArrowDown(&device) == 0 &&
        DLL_HoleDevice_GetArrowLeft(&device) == 0 && DLL_HoleDevice_GetArrowRight(&device) == 0, "arrows");
    require(DLL_HoleDevice_GetFinishButton(&device) == 0 && DLL_HoleDevice_GetStickButton(&device) == 0 &&
        DLL_HoleDevice_GetLeftClickButton(&device) == 0 && DLL_HoleDevice_GetRightClickButton(&device) == 0, "buttons");

    DLL_HoleDevice_SetSensitivity(&device, 12.5f);
    require(DLL_HoleDevice_GetSensitivity(&device) == 12.5f, "sensitivity round trip");
    DLL_HoleDevice_SetSensitivity(&device, 1000.0f);  // 255 - 1000 is clamped to 0.1 in GetPositon
    require(DLL_HoleDevice_GetPositon(&device) == 0.0f, "position with clamped divisor");
    DLL_HoleDevice_SetSensitivity(&device, std::numeric_limits<float>::quiet_NaN());
    require(std::isnan(DLL_HoleDevice_GetPositon(&device)), "native NaN clamp semantics");

    // StartThread: first call starts, second call is refused; StopThred joins.
    require(DLL_HoleDevice_StartThread(&device) == 1, "thread started");
    require(DLL_HoleDevice_StartThread(&device) == 0, "second start refused");
    DLL_HoleDevice_StopThred(&device);
    DLL_HoleDevice_StopThred(&device);  // stopping again does nothing
    require(DLL_HoleDevice_StartThread(&device) == 1, "restart after stop");

    DLL_HoleDevice_Delete(&device);
    require(device == nullptr, "deleted");
    std::puts("hole_device_contract ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAILED: %s\n", error.what());
    return 1;
}
