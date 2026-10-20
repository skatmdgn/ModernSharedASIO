#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>
#include <mmdeviceapi.h>
#include <string>
namespace modern_asio {
// Read HKCU OutputDeviceId; empty/missing selects eRender/eConsole default.
// When a pinned endpoint is absent/offline, uses the Windows default instead.
// Does not overwrite the pinned preference: next initialization may use it.
// May call COM only on an already initialized non-realtime thread.
HRESULT resolve_output_endpoint(IMMDeviceEnumerator* enumerator,
                                IMMDevice** endpoint,
                                std::wstring* actual_endpoint_id = nullptr,
                                bool* used_fallback = nullptr) noexcept;

// Once IASIO::init has selected the effective endpoint, the WASAPI worker
// locks that same ID for this ASIO session. Never re-resolve a different
// default between initial rate negotiation and client activation.
HRESULT resolve_exact_output_endpoint(IMMDeviceEnumerator* enumerator,
                                      const std::wstring& endpoint_id,
                                      IMMDevice** endpoint) noexcept;

// UI/control-thread only. Resolves the saved output preference (with the
// normal missing-headset fallback) and reports the actual Windows mix rate.
// Does not start playback or change the user's saved selection.
HRESULT inspect_selected_output_route(std::wstring& actual_id,
                                      std::uint32_t& sample_rate,
                                      bool& used_fallback) noexcept;

// Cubase IASIO::controlPanel non-realtime modal device picker.
// Choose Windows default or any active headset/speaker/output endpoint.
// Persists an IMMDevice ID (not a friendly name) to the current user only.
// Selection takes effect after Cubase reinitializes the ASIO driver.
bool show_output_device_picker(HWND host_parent) noexcept;
}
#endif
