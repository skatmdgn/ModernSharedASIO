#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "modern_asio/output_endpoint.hpp"
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace modern_asio {
namespace {
constexpr wchar_t kSettings[] = L"Software\\ModernSharedASIO";
constexpr wchar_t kDeviceValue[] = L"OutputDeviceId";
struct Choice { std::wstring id; std::wstring label; };
struct Panel { std::vector<Choice> choices; unsigned selected{0}; };
constexpr int kList = 101;

bool active_with_rate(IMMDevice* device, std::uint32_t rate) {
    if (!device) return false;
    DWORD state = 0;
    if (FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE) return false;
    if (!rate) return true;
    ComPtr<IAudioClient> audio;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER,
                                nullptr, reinterpret_cast<void**>(audio.GetAddressOf()))))
        return false;
    WAVEFORMATEX* mix = nullptr;
    const HRESULT hr = audio->GetMixFormat(&mix);
    const bool match = SUCCEEDED(hr) && mix && mix->nSamplesPerSec == rate &&
                       mix->nChannels >= 2;
    CoTaskMemFree(mix);
    return match;
}

INT_PTR CALLBACK panel_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        auto* panel = reinterpret_cast<Panel*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(panel));
        SetWindowTextW(dialog, L"Modern Shared Audio - Output device");
        RECT rect{};
        GetClientRect(dialog, &rect);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            12, 12, width - 24, height - 55, dialog,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kList)), instance, nullptr);
        HWND apply = CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, width - 184, height - 36,
            80, 25, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
            instance, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE, width - 94, height - 36,
            80, 25, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
            instance, nullptr);
        if (!list || !apply || !cancel) { EndDialog(dialog, 0); return TRUE; }
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (HWND child : {list, apply, cancel})
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (const auto& choice : panel->choices)
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.c_str()));
        SendMessageW(list, LB_SETCURSEL, panel->selected, 0);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const int command = LOWORD(wparam);
        if (command == IDOK || (command == kList && HIWORD(wparam) == LBN_DBLCLK)) {
            HWND list = GetDlgItem(dialog, kList);
            const auto selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
            EndDialog(dialog, selected == LB_ERR ? 0 : selected + 1);
            return TRUE;
        }
        if (command == IDCANCEL) { EndDialog(dialog, 0); return TRUE; }
    }
    if (message == WM_CLOSE) { EndDialog(dialog, 0); return TRUE; }
    return FALSE;
}
} // namespace

std::wstring configured_output_device_id() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettings, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return {};
    wchar_t value[1024]{};
    DWORD bytes = sizeof(value), type = 0;
    const auto result = RegQueryValueExW(key, kDeviceValue, nullptr, &type,
                                        reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
        bytes > sizeof(value)) return {};
    value[1023] = L'\0';
    return value;
}

HRESULT resolve_output_endpoint(IMMDeviceEnumerator* enumerator, IMMDevice** output,
                                std::wstring& opened_id,
                                std::uint32_t required_rate) {
    if (!enumerator || !output) return E_POINTER;
    *output = nullptr;
    opened_id.clear();
    ComPtr<IMMDevice> device;
    const auto preferred = configured_output_device_id();
    if (!preferred.empty()) {
        if (SUCCEEDED(enumerator->GetDevice(preferred.c_str(), device.GetAddressOf())) &&
            !active_with_rate(device.Get(), required_rate))
            device.Reset();
    }
    if (!device) {
        HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                         device.GetAddressOf());
        if (FAILED(hr)) return hr;
        if (!active_with_rate(device.Get(), required_rate))
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    LPWSTR raw_id = nullptr;
    const HRESULT hr = device->GetId(&raw_id);
    if (FAILED(hr)) return hr;
    opened_id = raw_id;
    CoTaskMemFree(raw_id);
    *output = device.Detach();
    return S_OK;
}

bool show_output_device_panel(HWND parent) {
    // The Cubase UI thread can already be in either COM apartment.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
    struct Uninit { HRESULT hr; ~Uninit() { if (SUCCEEDED(hr)) CoUninitialize(); } } guard{init};
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(enumerator.GetAddressOf()))))
        return false;
    Panel panel;
    panel.choices.push_back({{}, L"Follow Windows default output (automatic)"});
    const auto selected_id = configured_output_device_id();
    ComPtr<IMMDeviceCollection> devices;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL,
                                                  devices.GetAddressOf()))) {
        UINT count = 0;
        if (SUCCEEDED(devices->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device;
                if (FAILED(devices->Item(i, device.GetAddressOf()))) continue;
                LPWSTR id = nullptr;
                if (FAILED(device->GetId(&id))) continue;
                std::wstring device_id{id};
                CoTaskMemFree(id);
                DWORD state = 0;
                device->GetState(&state);
                std::wstring name{L"Audio endpoint"};
                ComPtr<IPropertyStore> properties;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
                    PROPVARIANT value{};
                    PropVariantInit(&value);
                    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
                        value.vt == VT_LPWSTR && value.pwszVal)
                        name.assign(value.pwszVal);
                    PropVariantClear(&value);
                }
                if (state != DEVICE_STATE_ACTIVE) name += L" (disconnected / disabled)";
                panel.choices.push_back({device_id, name});
                if (device_id == selected_id) panel.selected = static_cast<unsigned>(panel.choices.size() - 1);
            }
        }
    }
    if (!selected_id.empty() && panel.selected == 0) {
        panel.choices.push_back({selected_id, L"Previously selected device (unavailable)"});
        panel.selected = static_cast<unsigned>(panel.choices.size() - 1);
    }
    // No .rc resource or activation context is needed for this dialog.
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog{WS_CAPTION | WS_POPUP | WS_SYSMENU | DS_MODALFRAME, 0, 0, 0, 0, 265, 175};
        WORD menu{0};
        WORD window_class{0};
        WORD title{0};
    } layout;
    const INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), &layout.dialog, parent, panel_proc,
        reinterpret_cast<LPARAM>(&panel));
    if (result <= 0 || static_cast<std::size_t>(result) > panel.choices.size())
        return result == 0; // Cancel is still a successfully displayed panel.
    const auto& chosen = panel.choices[static_cast<std::size_t>(result - 1)].id;
    HKEY key = nullptr;
    const auto status = RegCreateKeyExW(HKEY_CURRENT_USER, kSettings, 0, nullptr,
                                       0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return false;
    const auto bytes = static_cast<DWORD>((chosen.size() + 1) * sizeof(wchar_t));
    const auto written = RegSetValueExW(key, kDeviceValue, 0, REG_SZ,
                      reinterpret_cast<const BYTE*>(chosen.c_str()), bytes);
    RegCloseKey(key);
    return written == ERROR_SUCCESS;
}
} // namespace modern_asio
#endif
