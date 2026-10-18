#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "modern_asio/output_device_selector.hpp"
#include "modern_asio/output_device_selection_policy.hpp"
#include <Windows.h>
#include <Objbase.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

using Microsoft::WRL::ComPtr;
namespace modern_asio {
namespace {
constexpr wchar_t kRegistrySubkey[]=L"Software\\ModernSharedASIO";
constexpr wchar_t kRegistryValue[]=L"OutputDeviceId";
constexpr int kDeviceList=2101;
constexpr int kRefresh=2102;

struct ComInit final {
    HRESULT result{CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)};
    ~ComInit(){if(SUCCEEDED(result))CoUninitialize();}
    // RPC_E_CHANGED_MODE means the calling Cubase host already initialized
    // COM differently; using its existing apartment is allowed.
    bool usable() const noexcept {return SUCCEEDED(result)||result==RPC_E_CHANGED_MODE;}
};
HRESULT read_device_choice(std::wstring& id) {
    id.clear();
    DWORD bytes=0;
    const auto status=RegGetValueW(HKEY_CURRENT_USER,kRegistrySubkey,kRegistryValue,
                                   RRF_RT_REG_SZ,nullptr,nullptr,&bytes);
    if(status==ERROR_FILE_NOT_FOUND||status==ERROR_PATH_NOT_FOUND)return S_FALSE;
    if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
    if(bytes<sizeof(wchar_t)||bytes>8192||bytes%sizeof(wchar_t))return E_INVALIDARG;
    std::vector<wchar_t> data(bytes/sizeof(wchar_t)+1,L'\0');
    DWORD read_bytes=bytes;
    const auto read_status=RegGetValueW(HKEY_CURRENT_USER,kRegistrySubkey,kRegistryValue,
                                        RRF_RT_REG_SZ,nullptr,data.data(),&read_bytes);
    if(read_status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(read_status);
    id.assign(data.data());
    return id.empty()?S_FALSE:S_OK;
}
HRESULT save_device_choice(const std::wstring& id) noexcept {
    HKEY key=nullptr;
    const auto opened=RegCreateKeyExW(HKEY_CURRENT_USER,kRegistrySubkey,0,nullptr,0,
                                      KEY_SET_VALUE,nullptr,&key,nullptr);
    if(opened!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(opened);
    LONG status=ERROR_SUCCESS;
    if(id.empty()) {
        status=RegDeleteValueW(key,kRegistryValue);
        if(status==ERROR_FILE_NOT_FOUND)status=ERROR_SUCCESS;
    } else {
        const auto bytes=(id.size()+1)*sizeof(wchar_t);
        if(bytes>8192)status=ERROR_INVALID_PARAMETER;
        else status=RegSetValueExW(key,kRegistryValue,0,REG_SZ,
                    reinterpret_cast<const BYTE*>(id.c_str()),
                    static_cast<DWORD>(bytes));
    }
    RegCloseKey(key);
    return status==ERROR_SUCCESS?S_OK:HRESULT_FROM_WIN32(status);
}
struct OutputDevice {
    std::wstring id;
    std::wstring title;
};
struct DialogState {
    std::vector<OutputDevice> devices;
    std::wstring chosen;
    HWND combo{nullptr};
    bool applied{false};
};
void show_error(HWND parent,const wchar_t* text) {
    MessageBoxW(parent,text,L"Modern Shared Audio",MB_OK|MB_ICONWARNING);
}
std::wstring friendly_name(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    if(FAILED(device->OpenPropertyStore(STGM_READ,properties.GetAddressOf())))
        return L"Unknown output";
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name=L"Unknown output";
    if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value)) &&
       value.vt==VT_LPWSTR && value.pwszVal)
       name=value.pwszVal;
    PropVariantClear(&value);
    return name;
}
void enumerate_devices(DialogState& state) {
    state.devices.clear();
    state.devices.push_back({L"",L"Windows default output (automatic)"});
    ComPtr<IMMDeviceEnumerator> enumerator;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,
             CLSCTX_INPROC_SERVER,IID_PPV_ARGS(enumerator.GetAddressOf()))))return;
    ComPtr<IMMDeviceCollection> collection;
    if(FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,
             collection.GetAddressOf())))return;
    UINT count=0;
    if(FAILED(collection->GetCount(&count)))return;
    // Limit the control-panel list to a sane number of active endpoints.
    for(UINT i=0;i<count && i<256;++i) {
        ComPtr<IMMDevice> device;
        if(FAILED(collection->Item(i,device.GetAddressOf())))continue;
        LPWSTR raw_id=nullptr;
        if(FAILED(device->GetId(&raw_id)))continue;
        std::wstring id=raw_id?raw_id:L"";
        CoTaskMemFree(raw_id);
        if(!id.empty())state.devices.push_back({std::move(id),friendly_name(device.Get())});
    }
}
void set_font(HWND control) {
    SendMessageW(control,WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
}
void refill_combo(HWND dialog,DialogState& state) {
    enumerate_devices(state);
    SendMessageW(state.combo,CB_RESETCONTENT,0,0);
    int current=0;
    bool found=state.chosen.empty();
    for(std::size_t i=0;i<state.devices.size();++i) {
        const auto& dev=state.devices[i];
        SendMessageW(state.combo,CB_ADDSTRING,0,
                     reinterpret_cast<LPARAM>(dev.title.c_str()));
        if(dev.id==state.chosen){current=static_cast<int>(i);found=true;}
    }
    SendMessageW(state.combo,CB_SETCURSEL,static_cast<WPARAM>(current),0);
    SetDlgItemTextW(dialog,2103,found
      ? L"Choose a device and click Save. Close and reopen Cubase to apply."
      : L"Saved output is disconnected. Select an available output, then restart Cubase.");
}
INT_PTR CALLBACK picker_proc(HWND dialog,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog,DWLP_USER));
    if(message==WM_INITDIALOG) {
        state=reinterpret_cast<DialogState*>(lp);
        SetWindowLongPtrW(dialog,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
        SetWindowTextW(dialog,L"Modern Shared Audio - Output device");
        RECT rect{};
        GetClientRect(dialog,&rect);
        const int width=rect.right-rect.left;
        const int left=14,usable=width-28;
        HWND label=CreateWindowExW(0,L"STATIC",L"Windows playback device:",
             WS_CHILD|WS_VISIBLE,left,12,usable,20,dialog,nullptr,nullptr,nullptr);
        set_font(label);
        state->combo=CreateWindowExW(0,L"COMBOBOX",L"",
             WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
             left,37,usable-92,240,dialog,reinterpret_cast<HMENU>(kDeviceList),nullptr,nullptr);
        set_font(state->combo);
        HWND refresh=CreateWindowExW(0,L"BUTTON",L"Refresh",
             WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
             width-92,37,78,25,dialog,reinterpret_cast<HMENU>(kRefresh),nullptr,nullptr);
        set_font(refresh);
        HWND help=CreateWindowExW(0,L"STATIC",L"",
             WS_CHILD|WS_VISIBLE,left,75,usable,37,dialog,
             reinterpret_cast<HMENU>(2103),nullptr,nullptr);
        set_font(help);
        HWND save=CreateWindowExW(0,L"BUTTON",L"Save",
             WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
             width-183,119,80,28,dialog,reinterpret_cast<HMENU>(IDOK),nullptr,nullptr);
        set_font(save);
        HWND cancel=CreateWindowExW(0,L"BUTTON",L"Cancel",
             WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
             width-96,119,80,28,dialog,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);
        set_font(cancel);
        refill_combo(dialog,*state);
        return TRUE;
    }
    if(message==WM_COMMAND && state) {
        const int command=LOWORD(wp);
        if(command==kRefresh){
            refill_combo(dialog,*state);
            return TRUE;
        }
        if(command==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
        if(command==IDOK){
            const LRESULT idx=SendMessageW(state->combo,CB_GETCURSEL,0,0);
            if(idx==CB_ERR || idx<0 || static_cast<std::size_t>(idx)>=state->devices.size()){
                show_error(dialog,L"Select an output device.");
                return TRUE;
            }
            const auto hr=save_device_choice(state->devices[static_cast<std::size_t>(idx)].id);
            if(FAILED(hr)) {
                show_error(dialog,L"Could not save output selection in the current user settings.");
                return TRUE;
            }
            state->applied=true;
            EndDialog(dialog,IDOK);
            return TRUE;
        }
    }
    if(message==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}
    return FALSE;
}
} // namespace

HRESULT resolve_output_endpoint(IMMDeviceEnumerator* enumerator,
                                IMMDevice** endpoint) noexcept {
    if(!enumerator||!endpoint)return E_POINTER;
    *endpoint=nullptr;
    try {
        std::wstring selected;
        const auto hr=read_device_choice(selected);
        if(FAILED(hr))return hr;
        return dispatch_output_endpoint(selected,
            [&]() noexcept -> HRESULT {
                return enumerator->GetDefaultAudioEndpoint(eRender,eConsole,endpoint);
            },
            [&](const std::wstring& id) noexcept -> HRESULT {
                // Never silently route explicit headset output to speakers.
                ComPtr<IMMDevice> device;
                auto result=enumerator->GetDevice(id.c_str(),device.GetAddressOf());
                if(FAILED(result))return result;
                DWORD state=0;
                result=device->GetState(&state);
                if(FAILED(result))return result;
                if((state&DEVICE_STATE_ACTIVE)==0)
                    return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
                return device.CopyTo(endpoint);
            });
    } catch(...) {return E_OUTOFMEMORY;}
}
bool show_output_device_picker(HWND host_parent) noexcept {
    try {
        ComInit com;
        if(!com.usable()) {
            show_error(nullptr,L"Could not initialize Windows audio device enumeration.");
            return false;
        }
        DialogState state{};
        const auto hr=read_device_choice(state.chosen);
        if(FAILED(hr)){
            show_error(nullptr,L"Could not read current output device settings.");
            return false;
        }
        // DS_SETFONT is intentionally absent; children receive a GUI font.
        // The resource has a null menu, null class, empty title and no controls.
        struct ModalTemplate { DLGTEMPLATE layout; WORD zero[4]{}; };
        ModalTemplate t{};
        t.layout.style=WS_CAPTION|WS_POPUP|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;
        t.layout.dwExtendedStyle=WS_EX_DLGMODALFRAME;
        t.layout.cdit=0;
        t.layout.cx=370;
        t.layout.cy=116;
        // Normalize invalid host handles to an unowned modal window.
        HWND parent=host_parent && IsWindow(host_parent)?host_parent:nullptr;
        const INT_PTR result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
              &t.layout,parent,picker_proc,reinterpret_cast<LPARAM>(&state));
        if(result==-1)show_error(parent,L"Could not open the ASIO output device control panel.");
        return result==IDOK && state.applied;
    } catch(...) {
        show_error(nullptr,L"Failed to display output device options.");
        return false;
    }
}
} // namespace modern_asio
#endif
