#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mmdeviceapi.h>
namespace modern_asio {
// Read HKCU OutputDeviceId; empty/missing selects eRender/eConsole default.
// An explicit-but-offline endpoint yields an error; never silently fallback.
// May call COM only on an already initialized non-realtime thread.
HRESULT resolve_output_endpoint(IMMDeviceEnumerator* enumerator,
                                IMMDevice** endpoint) noexcept;

// Cubase IASIO::controlPanel non-realtime modal device picker.
// Choose Windows default or any active headset/speaker/output endpoint.
// Persists an IMMDevice ID (not a friendly name) to the current user only.
// Selection takes effect after Cubase reinitializes the ASIO driver.
bool show_output_device_picker(HWND host_parent) noexcept;
}
#endif
