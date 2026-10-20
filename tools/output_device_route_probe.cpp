#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Objbase.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <string>
#include <cstdio>
#include "modern_asio/output_device_selector.hpp"

using Microsoft::WRL::ComPtr;
static std::wstring name_of(IMMDevice* device) {
    if(!device)return L"(none)";
    ComPtr<IPropertyStore> properties;
    if(FAILED(device->OpenPropertyStore(STGM_READ,properties.GetAddressOf())))
        return L"(unknown friendly name)";
    PROPVARIANT name;
    PropVariantInit(&name);
    std::wstring result=L"(unknown friendly name)";
    if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&name))
       && name.vt==VT_LPWSTR && name.pwszVal)
        result=name.pwszVal;
    PropVariantClear(&name);
    return result;
}
int wmain() {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com) && com!=RPC_E_CHANGED_MODE) {
        std::fwprintf(stderr,L"COM initialize failed: 0x%08X\n",static_cast<unsigned>(com));
        return 1;
    }
    const bool uninit=SUCCEEDED(com);
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(enumerator.GetAddressOf()));
    if(FAILED(hr)){
        std::fwprintf(stderr,L"Windows endpoint enumerator failed: 0x%08X\n",static_cast<unsigned>(hr));
        if(uninit)CoUninitialize();
        return 2;
    }
    ComPtr<IMMDevice> current_default;
    const HRESULT default_hr=enumerator->GetDefaultAudioEndpoint(
        eRender,eConsole,current_default.GetAddressOf());
    if(SUCCEEDED(default_hr))
        std::wprintf(L"Windows default playback : %ls\n",name_of(current_default.Get()).c_str());
    else
        std::wprintf(L"Windows default playback : unavailable (0x%08X)\n",
                     static_cast<unsigned>(default_hr));
    ComPtr<IMMDevice> chosen;
    std::wstring id;
    bool fallback=false;
    hr=modern_asio::resolve_output_endpoint(
        enumerator.Get(),chosen.GetAddressOf(),&id,&fallback);
    if(FAILED(hr)) {
        std::wprintf(L"ModernSharedASIO planned output: unavailable (0x%08X)\n",
                     static_cast<unsigned>(hr));
    } else {
        std::wprintf(L"ModernSharedASIO planned output: %ls\n",
                     name_of(chosen.Get()).c_str());
        std::wprintf(L"Fallback from saved headset: %ls\n",fallback?L"YES":L"NO");
        std::wprintf(L"Resolved device ID retained: %ls\n",id.empty()?L"NO":L"YES");
    }
    std::puts("This probe reads the preferred output and endpoint state only.");
    std::puts("It cannot prove which DLL/version Cubase has loaded or which endpoint is actually rendering.");
    chosen.Reset();
    current_default.Reset();
    enumerator.Reset();
    if(uninit)CoUninitialize();
    return FAILED(hr)?3:0;
}
