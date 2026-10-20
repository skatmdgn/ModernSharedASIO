#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "modern_asio/output_device_change_monitor.hpp"
#include <mmdeviceapi.h>
#include <objbase.h>
#include <wrl/client.h>
#include <atomic>
#include <new>
#include <utility>

namespace modern_asio {
namespace {
using Microsoft::WRL::ComPtr;

// May be invoked on arbitrary COM/Windows audio service threads. Never
// resolve devices, open audio clients, join threads, or take driver locks here.
class EndpointSignal final : public IMMNotificationClient {
public:
    explicit EndpointSignal(HANDLE event) noexcept : changed_(event) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(IsEqualIID(iid,IID_IUnknown) || IsEqualIID(iid,__uuidof(IMMNotificationClient))) {
            *out=static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1,std::memory_order_relaxed)+1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining=references_.fetch_sub(1,std::memory_order_acq_rel)-1;
        if(!remaining)delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        SetEvent(changed_);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        SetEvent(changed_);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        SetEvent(changed_);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if(flow==eRender && role==eConsole)SetEvent(changed_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        return S_OK;
    }
private:
    std::atomic<ULONG> references_{1};
    HANDLE changed_;
};
} // namespace

bool OutputDeviceChangeMonitor::start(std::function<void()> on_change) noexcept {
    if(worker_.joinable() || stop_event_ || changed_event_ || !on_change)return false;
    stop_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    changed_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!stop_event_ || !changed_event_){stop();return false;}
    try {
        on_change_=std::move(on_change);
        worker_=std::thread([this]() noexcept {run();});
        return true;
    } catch(...) {
        stop();
        return false;
    }
}
void OutputDeviceChangeMonitor::stop() noexcept {
    if(stop_event_)SetEvent(stop_event_);
    if(worker_.joinable())worker_.join();
    on_change_={};
    if(changed_event_){CloseHandle(changed_event_);changed_event_=nullptr;}
    if(stop_event_){CloseHandle(stop_event_);stop_event_=nullptr;}
}
void OutputDeviceChangeMonitor::run() noexcept {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init))return;
    ComPtr<IMMDeviceEnumerator> devices;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(devices.GetAddressOf()));
    if(SUCCEEDED(hr)) {
        auto* signal=new(std::nothrow) EndpointSignal(changed_event_);
        if(signal) {
            const HRESULT registered=devices->RegisterEndpointNotificationCallback(signal);
            if(SUCCEEDED(registered)) {
                // Reconcile the window between ASIO init and registration.
                SetEvent(changed_event_);
                HANDLE events[2]{stop_event_,changed_event_};
                for(;;) {
                    const DWORD wait=WaitForMultipleObjects(2,events,FALSE,INFINITE);
                    if(wait==WAIT_OBJECT_0)break;
                    if(wait!=WAIT_OBJECT_0+1)break;
                    // Audio devices commonly emit several remove/state/default
                    // notifications. Coalesce those on this NON-audio thread.
                    DWORD quiet;
                    do {
                        quiet=WaitForMultipleObjects(2,events,FALSE,125);
                        if(quiet==WAIT_OBJECT_0)break;
                    } while(quiet==WAIT_OBJECT_0+1);
                    if(quiet==WAIT_OBJECT_0)break;
                    if(quiet!=WAIT_TIMEOUT)break;
                    try {if(on_change_)on_change_();} catch(...) {}
                }
                devices->UnregisterEndpointNotificationCallback(signal);
            }
            signal->Release();
        }
    }
    CoUninitialize();
}
} // namespace modern_asio
#endif
