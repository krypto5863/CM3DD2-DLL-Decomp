#include "cm3d2/hole_device_api.h"

#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// Reconstructed from DLL_HoleDevice_* (0x18006ea20..0x18006eeb0) and their helpers:
// FUN_180094150 (HID search), FUN_180093c70 (reader thread), FUN_180093b80 (stop),
// FUN_1800936d0 / FUN_180093af0 (DirectInput setup / enumeration callback) and FUN_180093970 (poll).
// The original object is 0x3a8 bytes; the original offsets are noted next to each member.
// UNVERIFIED: behaviour against real hardware (VID 0x1346, PID 0x7011); only state logic is tested.
namespace {

// DAT_1801bdd20: IID_IDirectInput8W.
const GUID kIidDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr USHORT kVendorId = 0x1346;
constexpr USHORT kProductId = 0x7011;

std::atomic<int> g_instance_count{0};  // _DAT_18021dc58

struct HoleDevice {
    wchar_t path[256] = {};                     // +0x000 HID device path
    std::uint32_t input_report_length = 0;      // +0x200 HIDP_CAPS.InputReportByteLength
    std::uint32_t output_report_length = 0;     // +0x204
    std::atomic<bool> hid_found{false};         // +0x208
    std::atomic<bool> input_ready{false};       // +0x209 DirectInput device available
    std::atomic<float> pressure{0};             // +0x20c smoothed raw pressure
    std::atomic<float> plane_position{0};       // +0x210
    std::atomic<float> deviation{0};            // +0x214
    std::atomic<float> seconds_speed{0};        // +0x218
    std::atomic<float> speed_counter{0};        // +0x21c
    std::atomic<float> sensitivity{0};          // +0x220
    std::atomic<bool> running{false};           // +0x224
    std::thread thread;                         // +0x228
    std::mutex lock;                            // +0x230
    HMODULE module = nullptr;                   // +0x280
    IDirectInput8W* direct_input = nullptr;     // +0x288
    IDirectInputDevice8W* joystick = nullptr;   // +0x290
    DIJOYSTATE2 state = {};                     // +0x298 (0x110 bytes)
};

HoleDevice* device(CM3D2_HoleDeviceHandle* handle) { return static_cast<HoleDevice*>(*handle); }

bool inputActive(const HoleDevice* d) { return d->hid_found && d->input_ready; }

// FUN_180093af0: the first attached game controller with the PIDVID product GUID of the device.
struct EnumContext {
    IDirectInput8W* direct_input;
    IDirectInputDevice8W* joystick;
};

BOOL CALLBACK enumCallback(const DIDEVICEINSTANCEW* instance, void* context) {
    auto* enumeration = static_cast<EnumContext*>(context);
    const GUID& product = instance->guidProduct;
    std::uint64_t head = 0;
    std::uint64_t tail = 0;
    std::memcpy(&head, &product, 8);
    std::memcpy(&tail, product.Data4, 8);
    if (head == 0x70111346 && tail == 0x4449564449500000ull) {
        IDirectInputDevice8W* created = nullptr;
        if (SUCCEEDED(enumeration->direct_input->CreateDevice(instance->guidInstance, &created, nullptr))) {
            enumeration->joystick = created;
            return DIENUM_STOP;
        }
        if (created != nullptr) {
            created->Release();
        }
    }
    return DIENUM_CONTINUE;
}

// FUN_180093970: poll the joystick state; on failure re-acquire once, then fall back to zeros.
void pollState(HoleDevice* d) {
    if (d->joystick == nullptr || d->direct_input == nullptr) {
        return;
    }
    if (FAILED(d->joystick->GetDeviceState(sizeof(d->state), &d->state))) {
        std::memset(&d->state, 0, sizeof(d->state));
        d->joystick->Acquire();
        if (FAILED(d->joystick->GetDeviceState(sizeof(d->state), &d->state))) {
            std::memset(&d->state, 0, sizeof(d->state));
        }
    }
}

// FUN_1800936d0.
bool initDirectInput(HoleDevice* d) {
    if (d->direct_input != nullptr) {
        return true;
    }
    IDirectInput8W* direct_input = nullptr;
    if (FAILED(DirectInput8Create(d->module, 0x800, kIidDirectInput8W, reinterpret_cast<void**>(&direct_input), nullptr))) {
        return false;
    }
    d->direct_input = direct_input;
    EnumContext context{direct_input, nullptr};
    const HRESULT enumerated = direct_input->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCallback, &context, DIEDFL_ATTACHEDONLY);
    if (d->joystick != nullptr) {
        d->joystick->Release();
    }
    d->joystick = context.joystick;
    const auto release_all = [&]() {
        // FUN_1800936d0 releases the DirectInput object first, then the device.
        if (d->direct_input != nullptr) {
            d->direct_input->Release();
            d->direct_input = nullptr;
        }
        if (d->joystick != nullptr) {
            d->joystick->Release();
            d->joystick = nullptr;
        }
    };
    if (FAILED(enumerated) || d->joystick == nullptr ||
        FAILED(d->joystick->SetDataFormat(&c_dfDIJoystick2)) ||
        FAILED(d->joystick->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) {
        release_all();
        return false;
    }
    pollState(d);
    return true;
}

// FUN_180094150.
void checkHidDevice(HoleDevice* d) {
    d->input_ready = initDirectInput(d);
    if (!d->input_ready) {
        return;
    }
    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);
    HDEVINFO devices = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        d->hid_found = false;
        return;
    }
    bool found = false;
    for (DWORD index = 0; !found; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &hid_guid, index, &interface_data)) {
            break;
        }
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, nullptr, 0, &needed, nullptr);
        if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<char> storage(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        DWORD written = 0;
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail, needed, &written, nullptr)) continue;
        HANDLE handle = CreateFileW(detail->DevicePath, 0, 3, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (HidD_GetAttributes(handle, &attributes) &&
            attributes.VendorID == kVendorId && attributes.ProductID == kProductId) {
            // FUN_180094150 keeps the query handle of a VID/PID match open and never frees the preparsed data; the
            // GENERIC_WRITE handle it opens for a matching top-level collection is also never closed.
            PHIDP_PREPARSED_DATA preparsed = nullptr;
            HIDP_CAPS caps{};
            if (HidD_GetPreparsedData(handle, &preparsed)) {
                HidP_GetCaps(preparsed, &caps);  // the result is not checked (the fields stay zero on failure)
                if (caps.UsagePage == 0xFF00 && caps.Usage == 1) {
                    CreateFileW(detail->DevicePath, GENERIC_WRITE, 3, nullptr, OPEN_EXISTING, 0, nullptr);
                    d->input_report_length = caps.InputReportByteLength;
                    d->output_report_length = caps.OutputReportByteLength;
                    wcsncpy_s(d->path, detail->DevicePath, _TRUNCATE);  // original: unbounded lstrcpyW (not reproducible safely)
                    found = true;
                }
            }
        } else {
            CloseHandle(handle);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    d->hid_found = found;
}

// FUN_180093c70. The original loop is reproduced as is: it issues an overlapped ReadFile every iteration, never resets the event
// and only processes a report when the request had already completed (OVERLAPPED.Internal == 0 and InternalHigh != 0) at that point.
// The original reads InputReportByteLength bytes into a 32-byte stack buffer and leaves that buffer and the OVERLAPPED behind when
// it returns while a request is still pending; here both live in a heap block that is never freed, sized for the whole report, so
// the same reads cannot corrupt memory.
struct ReaderBlock {
    OVERLAPPED overlapped = {};
    std::vector<std::uint8_t> report;
    DWORD bytes = 0;
};

void readerThread(HoleDevice* d) {
    {
        std::lock_guard<std::mutex> guard(d->lock);
        d->pressure = 0;
        d->plane_position = 0;
        d->deviation = 0;
        // Native restart leaves seconds_speed and speed_counter unchanged.
    }
    auto* block = new ReaderBlock;  // intentionally leaked (see above)
    HANDLE file = nullptr;
    bool running = d->running;
    while (running) {
        bool read_now = file != nullptr;
        if (file == nullptr) {
            if (!d->hid_found || !d->input_ready) {
                checkHidDevice(d);
            }
            if (d->hid_found && d->input_ready) {
                file = CreateFileW(d->path, GENERIC_READ, 3, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (file == nullptr || file == INVALID_HANDLE_VALUE) {
                    Sleep(50);
                    return;  // the original returns here without cleanup
                }
                block->report.assign(std::max<std::size_t>(32, d->input_report_length), 0);
                block->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                read_now = true;
            } else {
                Sleep(50);
            }
        }
        if (read_now) {
            ReadFile(file, block->report.data(), d->input_report_length, &block->bytes, &block->overlapped);
            if (block->overlapped.hEvent != nullptr && block->overlapped.Internal == 0 && block->overlapped.InternalHigh != 0 &&
                WaitForSingleObject(block->overlapped.hEvent, INFINITE) == WAIT_OBJECT_0) {
                std::lock_guard<std::mutex> guard(d->lock);
                const std::uint8_t* report = block->report.data();
                if (report[1] == 0xFF) {
                    if (report[2] == 1) {
                        std::printf("come here A \n");
                    }
                    if (report[2] == 4) {
                        std::printf("come here B \n");
                    }
                }
                const float raw = static_cast<float>(report[2]);
                const float old_pressure = d->pressure;
                float smoothed = raw * 0.1f + old_pressure * 0.9f;
                if (old_pressure + smoothed < 0.0001f) {
                    smoothed = 0.0f;
                }
                d->pressure = smoothed;
                float position = 0.0f;
                float push = 0.0f;
                if (raw > 1.0f) {
                    position = 255.0f - static_cast<float>(report[1]);
                    push = position * 0.05f;
                }
                const float old_position = d->plane_position;
                d->plane_position = position;
                if (position < 0.001f) {
                    d->plane_position = 0.0f;
                    position = 0.0f;
                }
                float deviation = std::fabs(position - (old_position * 0.95f + push)) * 0.05f + d->deviation * 0.95f;
                d->deviation = deviation;
                if (deviation < 0.001f) {
                    d->deviation = 0.0f;
                    deviation = 0.0f;
                }
                float counter;
                if (d->speed_counter >= 0.0f) {
                    counter = d->speed_counter - 1.0f;
                } else {
                    d->seconds_speed = deviation;
                    counter = 100.0f;
                }
                d->speed_counter = counter;
            }
        }
        running = d->running;
    }
    if (block->overlapped.hEvent != nullptr) {
        CloseHandle(block->overlapped.hEvent);
    }
    block->overlapped.hEvent = nullptr;
    if (file != nullptr) {
        CloseHandle(file);
    }
}

// FUN_180093b80.
void stopThread(HoleDevice* d) {
    if (d->thread.joinable() && d->running) {
        {
            std::lock_guard<std::mutex> guard(d->lock);
            d->running = false;
        }
        d->thread.join();
    }
}

// COMISS/MINSS clamps of GetPositon (0x18006ecd0): MINSS returns its second operand when either is NaN,
// so a NaN passes through; a value below the low bound is replaced.
float minss(float a, float b) { return a < b ? a : b; }
float spanFor(float sensitivity) {
    const float difference = 255.0f - sensitivity;
    return 0.1f > difference ? 0.1f : minss(255.0f, difference);
}
float unitClamp(float value) { return 0.0f > value ? 0.0f : minss(0.9999f, value); }

// Buttons are bit 7 of the DirectInput button bytes: finish 2, stick 5, left click 0, right click 1.
uint8_t button(CM3D2_HoleDeviceHandle* h, int index) {
    const HoleDevice* d = device(h);
    return inputActive(d) ? static_cast<uint8_t>(d->state.rgbButtons[index] >> 7) : 0;
}

// Arrows are axis values of the DirectInput state: up lX (+0x298), left lY (+0x29c),
// right lRx (+0x2a4), down lRy (+0x2a8).
int32_t axis(CM3D2_HoleDeviceHandle* h, LONG DIJOYSTATE2::*member) {
    const HoleDevice* d = device(h);
    return inputActive(d) ? static_cast<int32_t>(d->state.*member) : 0;
}

} // namespace

extern "C" {

void CM3D2_CALL DLL_HoleDevice_Create(CM3D2_HoleDeviceHandle* out_device) {
    auto* d = new HoleDevice;
    d->module = GetModuleHandleW(nullptr);
    ++g_instance_count;
    *out_device = d;
}

void CM3D2_CALL DLL_HoleDevice_Delete(CM3D2_HoleDeviceHandle* handle) {
    auto* d = static_cast<HoleDevice*>(*handle);
    if (d != nullptr) {
        stopThread(d);
        if (d->thread.joinable()) {
            d->thread.join();  // the thread ended by itself; the original leaks its record
        }
        if (d->joystick != nullptr) d->joystick->Release();
        if (d->direct_input != nullptr) d->direct_input->Release();
        delete d;
    }
    *handle = nullptr;
    --g_instance_count;
}

void CM3D2_CALL DLL_HoleDevice_CheckHidDevice(CM3D2_HoleDeviceHandle* h) { checkHidDevice(device(h)); }

void CM3D2_CALL DLL_HoleDevice_UpdateFrame(CM3D2_HoleDeviceHandle* h) {
    if (inputActive(device(h))) {
        pollState(device(h));
    }
}

uint32_t CM3D2_CALL DLL_HoleDevice_StartThread(CM3D2_HoleDeviceHandle* h) {
    auto* d = device(h);
    if (d->thread.joinable()) {
        return 0;
    }
    d->running = true;
    d->thread = std::thread(readerThread, d);
    return 1;
}

void CM3D2_CALL DLL_HoleDevice_StopThred(CM3D2_HoleDeviceHandle* h) { stopThread(device(h)); }
void CM3D2_CALL DLL_HoleDevice_SetSensitivity(CM3D2_HoleDeviceHandle* h, float value) { device(h)->sensitivity = value; }
uint32_t CM3D2_CALL DLL_HoleDevice_IsEnabled(CM3D2_HoleDeviceHandle* h) { return inputActive(device(h)); }
float CM3D2_CALL DLL_HoleDevice_GetPressure(CM3D2_HoleDeviceHandle* h) { return device(h)->pressure; }

float CM3D2_CALL DLL_HoleDevice_GetPositon(CM3D2_HoleDeviceHandle* h) {
    const HoleDevice* d = device(h);
    return unitClamp(d->plane_position / spanFor(d->sensitivity));
}

float CM3D2_CALL DLL_HoleDevice_GetLaserPosition(CM3D2_HoleDeviceHandle* h) {
    return unitClamp(device(h)->plane_position / 255.0f);
}

float CM3D2_CALL DLL_HoleDevice_GetSensitivity(CM3D2_HoleDeviceHandle* h) { return device(h)->sensitivity; }
uint32_t CM3D2_CALL DLL_HoleDevice_GetFinishButton(CM3D2_HoleDeviceHandle* h) { return button(h, 2); }
uint32_t CM3D2_CALL DLL_HoleDevice_GetStickButton(CM3D2_HoleDeviceHandle* h) { return button(h, 5); }
uint32_t CM3D2_CALL DLL_HoleDevice_GetLeftClickButton(CM3D2_HoleDeviceHandle* h) { return button(h, 0); }
uint32_t CM3D2_CALL DLL_HoleDevice_GetRightClickButton(CM3D2_HoleDeviceHandle* h) { return button(h, 1); }
float CM3D2_CALL DLL_HoleDevice_GetSecondsSpeed(CM3D2_HoleDeviceHandle* h) { return device(h)->seconds_speed; }
float CM3D2_CALL DLL_HoleDevice_GetPlanePosition(CM3D2_HoleDeviceHandle* h) { return device(h)->plane_position; }
int32_t CM3D2_CALL DLL_HoleDevice_GetArrowUp(CM3D2_HoleDeviceHandle* h) { return axis(h, &DIJOYSTATE2::lX); }
int32_t CM3D2_CALL DLL_HoleDevice_GetArrowDown(CM3D2_HoleDeviceHandle* h) { return axis(h, &DIJOYSTATE2::lRy); }
int32_t CM3D2_CALL DLL_HoleDevice_GetArrowRight(CM3D2_HoleDeviceHandle* h) { return axis(h, &DIJOYSTATE2::lRx); }
int32_t CM3D2_CALL DLL_HoleDevice_GetArrowLeft(CM3D2_HoleDeviceHandle* h) { return axis(h, &DIJOYSTATE2::lY); }

} // extern "C"
