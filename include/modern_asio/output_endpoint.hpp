#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mmdeviceapi.h>
#include <cstdint>
#include <string>

namespace modern_asio {
// Empty OutputDeviceId means follow the Windows default console render endpoint.
// A missing/inactive selection falls back to the default automatically.
std::wstring configured_output_device_id();
HRESULT resolve_output_endpoint(IMMDeviceEnumerator* enumerator, IMMDevice** endpoint,
                                std::wstring& opened_id,
                                std::uint32_t required_sample_rate = 0);
// A modal, native Windows selection dialog. Only called on the host control thread.
bool show_output_device_panel(HWND parent);
} // namespace modern_asio
#endif
