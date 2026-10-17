// Experimental x64 ASIO 2.3 COM driver. Windows build/Cubase runtime NOT VERIFIED.
// Requires external Steinberg ASIO SDK common/iasiodrv.h under applicable license.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <iasiodrv.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <thread>
#include <vector>
#include "modern_asio/asio_playback_bridge.hpp"
#include "modern_asio/wasapi_shared_renderer.hpp"

using Microsoft::WRL::ComPtr;
namespace {
constexpr CLSID kCLSID{0x6fa63f59,0xa581,0x4d9a,{0xb8,0xcb,0x73,0xc3,0xbd,0x3a,0x6d,0x46}};
constexpr wchar_t kCLSIDText[] = L"{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}";
constexpr wchar_t kAsioReg[] = L"SOFTWARE\\ASIO\\Modern Shared Audio";
constexpr wchar_t kClassReg[] = L"Software\\Classes\\CLSID\\{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}";
constexpr long kFrames=512;
HMODULE g_module=nullptr;
std::atomic<long> g_objects{0},g_locks{0};

void write64(ASIOSamples& s, std::uint64_t x) noexcept {
    s.lo=static_cast<unsigned long>(x); s.hi=static_cast<unsigned long>(x>>32);
}
void write64(ASIOTimeStamp& s, std::uint64_t x) noexcept {
    s.lo=static_cast<unsigned long>(x); s.hi=static_cast<unsigned long>(x>>32);
}
std::uint64_t qpc_ns() noexcept {
    LARGE_INTEGER now{},rate{};
    if(!QueryPerformanceCounter(&now)||!QueryPerformanceFrequency(&rate)||rate.QuadPart<=0)return 0;
    auto n=static_cast<std::uint64_t>(now.QuadPart),d=static_cast<std::uint64_t>(rate.QuadPart);
    return (n/d)*1000000000ULL+((n%d)*1000000000ULL)/d;
}
void put_text(char* dest,const char* value,std::size_t max) noexcept {
    if(dest && max) std::snprintf(dest,max,"%s",value);
}
HRESULT reg_text(HKEY key,const wchar_t* name,const wchar_t* s) noexcept {
    return HRESULT_FROM_WIN32(RegSetValueExW(key,name,0,REG_SZ,
        reinterpret_cast<const BYTE*>(s),static_cast<DWORD>((std::wcslen(s)+1)*sizeof(wchar_t))));
}
class Driver final : public IASIO {
public:
    Driver():render_(fifo_, &Driver::request_audio, this, kFrames),bridge_(fifo_) {g_objects.fetch_add(1);}
    ~Driver(){stop();g_objects.fetch_sub(1);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** ppv) override {
        if(!ppv)return E_POINTER;
        *ppv=nullptr;
        // Steinberg's ASIO host requests the driver CLSID as its interface IID.
        if(IsEqualIID(riid,IID_IUnknown)||IsEqualIID(riid,kCLSID)){
            *ppv=static_cast<IASIO*>(this);AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ref_.fetch_add(1)+1;}
    ULONG STDMETHODCALLTYPE Release() override {
        auto n=ref_.fetch_sub(1)-1;if(!n)delete this;return n;
    }
    ASIOBool init(void*) override {
        if(initialized_)return ASIOTrue;
        // COM must stay initialized until all dependent interface pointers are released.
        struct CoGuard {
            HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
            ~CoGuard(){if(SUCCEEDED(hr))CoUninitialize();}
        } scope;
        if(FAILED(scope.hr)&&scope.hr!=RPC_E_CHANGED_MODE)return ASIOFalse;
        ComPtr<IMMDeviceEnumerator> e;
        ComPtr<IMMDevice> dev;
        ComPtr<IAudioClient> client;
        WAVEFORMATEX* fmt=nullptr;
        HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(e.GetAddressOf()));
        if(SUCCEEDED(hr))hr=e->GetDefaultAudioEndpoint(eRender,eConsole,dev.GetAddressOf());
        if(SUCCEEDED(hr))hr=dev->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,
                                          reinterpret_cast<void**>(client.GetAddressOf()));
        if(SUCCEEDED(hr))hr=client->GetMixFormat(&fmt);
        if(SUCCEEDED(hr) && fmt && fmt->nChannels>=2 && fmt->nSamplesPerSec>0){
            rate_=fmt->nSamplesPerSec;initialized_=true;
        }
        CoTaskMemFree(fmt);
        if(!initialized_)put_text(error_,"Windows shared stereo device unavailable",sizeof(error_));
        return initialized_?ASIOTrue:ASIOFalse;
    }
    void getDriverName(char* s) override {put_text(s,"Modern Shared Audio",32);}
    long getDriverVersion() override {return 0x0200;}
    void getErrorMessage(char* s) override {put_text(s,error_,124);}
    ASIOError start() override {
        if(!initialized_||!prepared_||playing_)return ASE_InvalidMode;
        fifo_.clear_stopped();
        position_.store(0,std::memory_order_release);
        buffer_index_=0;
        // ASIO host can prefill output half B before start.
        const auto initial=bridge_.submit(active_[0]?planar_[0][1].data():nullptr,
                                          active_[1]?planar_[1][1].data():nullptr,kFrames);
        if(initial!=kFrames)return ASE_HWMalfunction;
        position_.store(kFrames,std::memory_order_release);
        // Stream is active during backend start: its worker may call the host.
        playing_.store(true,std::memory_order_release);
        auto hr=render_.start(0);
        if(FAILED(hr)){
            playing_.store(false,std::memory_order_release);
            put_text(error_,"WASAPI shared-mode startup failed",sizeof(error_));
            return ASE_HWMalfunction;
        }
        if(render_.device_info().sample_rate!=rate_){
            render_.stop();
            playing_.store(false,std::memory_order_release);
            put_text(error_,"Device mix sample rate changed; reload driver",sizeof(error_));
            return ASE_NoClock;
        }
        return ASE_OK;
    }
    ASIOError stop() override {
        // A synchronous host Stop from inside its own callback is unsupported.
        // Never attempt to join/free a still-running WASAPI callback thread.
        if(render_.on_render_thread())return ASE_InvalidMode;
        render_.stop();
        playing_.store(false,std::memory_order_release);
        return ASE_OK;
    }
    ASIOError getChannels(long* in,long* out) override {
        if(!in||!out)return ASE_InvalidParameter;
        *in=0;*out=2;return ASE_OK;
    }
    ASIOError getLatencies(long* in,long* out) override {
        if(!in||!out)return ASE_InvalidParameter;
        *in=0;
        // Provisional bound only, not measured end-to-end latency.
        *out=2*kFrames+static_cast<long>(render_.device_info().buffer_frames);
        return ASE_OK;
    }
    ASIOError getBufferSize(long* min,long* max,long* preferred,long* step) override {
        if(!min||!max||!preferred||!step)return ASE_InvalidParameter;
        *min=*max=*preferred=kFrames;*step=0;return ASE_OK;
    }
    ASIOError canSampleRate(ASIOSampleRate x) override {
        return initialized_ && std::isfinite(x) && x==static_cast<double>(rate_)?ASE_OK:ASE_NoClock;
    }
    ASIOError getSampleRate(ASIOSampleRate* out) override {
        if(!out)return ASE_InvalidParameter;
        if(!initialized_)return ASE_NoClock;
        *out=static_cast<double>(rate_);return ASE_OK;
    }
    ASIOError setSampleRate(ASIOSampleRate x) override {return canSampleRate(x);}
    ASIOError getClockSources(ASIOClockSource* cs,long* count) override {
        if(!count)return ASE_InvalidParameter;
        if(!cs||*count<1){*count=1;return cs?ASE_InvalidParameter:ASE_OK;}
        std::memset(cs,0,sizeof(*cs));
        cs[0].index=0;cs[0].associatedChannel=-1;cs[0].associatedGroup=-1;
        cs[0].isCurrentSource=ASIOTrue;
        put_text(cs[0].name,"Windows shared clock",sizeof(cs[0].name));
        *count=1;return ASE_OK;
    }
    ASIOError setClockSource(long i) override {return i==0?ASE_OK:ASE_InvalidParameter;}
    ASIOError getSamplePosition(ASIOSamples* samples,ASIOTimeStamp* clock) override {
        if(!samples||!clock)return ASE_InvalidParameter;
        write64(*samples,position_.load(std::memory_order_relaxed));write64(*clock,qpc_ns());
        return ASE_OK;
    }
    ASIOError getChannelInfo(ASIOChannelInfo* info) override {
        if(!info||info->isInput||info->channel<0||info->channel>1)return ASE_InvalidParameter;
        info->isActive=active_[info->channel]?ASIOTrue:ASIOFalse;
        info->channelGroup=0;info->type=ASIOSTFloat32LSB;
        put_text(info->name,info->channel?"Output Right":"Output Left",sizeof(info->name));
        return ASE_OK;
    }
    ASIOError createBuffers(ASIOBufferInfo* info,long count,long frames,ASIOCallbacks* cb) override {
        if(!initialized_||prepared_||playing_)return ASE_InvalidMode;
        if(!info||count<1||count>2||frames!=kFrames||!cb||!cb->bufferSwitch)return ASE_InvalidParameter;
        bool selected[2]{false,false};
        for(long i=0;i<count;++i){
            const long c=info[i].channelNum;
            if(info[i].isInput||c<0||c>=2||selected[c])return ASE_InvalidParameter;
            selected[c]=true;
        }
        try {
            for(long c=0;c<2;c++)for(long b=0;b<2;b++)planar_[c][b].assign(kFrames,0.0f);
        } catch (...) {return ASE_NoMemory;}
        for(long i=0;i<count;i++)for(long b=0;b<2;b++)
            info[i].buffers[b]=planar_[info[i].channelNum][b].data();
        active_[0]=selected[0];active_[1]=selected[1];
        callbacks_=cb;
        time_info_=cb->asioMessage && cb->bufferSwitchTimeInfo &&
            cb->asioMessage(kAsioSupportsTimeInfo,0,nullptr,nullptr)==1;
        position_.store(0);buffer_index_=0;prepared_=true;return ASE_OK;
    }
    ASIOError disposeBuffers() override {
        if(playing_)return ASE_InvalidMode;
        if(!prepared_)return ASE_InvalidMode;
        callbacks_=nullptr;prepared_=false;active_[0]=active_[1]=false;
        for(auto& pair:planar_)for(auto& v:pair)v.clear();
        return ASE_OK;
    }
    ASIOError controlPanel() override {return ASE_NotPresent;}
    ASIOError future(long,void*) override {return ASE_NotPresent;}
    ASIOError outputReady() override {return ASE_OK;}
private:
    static bool request_audio(void* raw) noexcept {
        return static_cast<Driver*>(raw)->produce_one_block();
    }
    bool produce_one_block() noexcept {
        // Only the WASAPI render worker calls this after start().
        if(!prepared_||!callbacks_)return false;
        const long index=buffer_index_;
        try {
            if(time_info_ && callbacks_->bufferSwitchTimeInfo){
                ASIOTime time{};
                time.timeInfo.speed=1.;
                time.timeInfo.sampleRate=static_cast<double>(rate_);
                time.timeInfo.flags=kSystemTimeValid|kSamplePositionValid|kSampleRateValid;
                write64(time.timeInfo.samplePosition,position_.load(std::memory_order_relaxed));
                write64(time.timeInfo.systemTime,qpc_ns());
                callbacks_->bufferSwitchTimeInfo(&time,index,ASIOTrue);
            } else {
                callbacks_->bufferSwitch(index,ASIOTrue);
            }
        } catch (...) {
            callback_errors_.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        const auto accepted=bridge_.submit(active_[0]?planar_[0][index].data():nullptr,
                                           active_[1]?planar_[1][index].data():nullptr,kFrames);
        if(accepted!=kFrames){
            callback_errors_.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        position_.fetch_add(kFrames,std::memory_order_release);
        buffer_index_^=1;
        return true;
    }
    std::atomic<ULONG> ref_{1};
    modern_asio::SpscStereoRing fifo_{32768};
    modern_asio::WasapiSharedRenderer render_;
    modern_asio::AsioPlaybackBridge bridge_;
    // Control thread owns playing_; render callback never mutates it.
    std::atomic<bool> playing_{false};
    long buffer_index_{0};
    std::atomic<std::uint64_t> callback_errors_{0};
    std::atomic<std::uint64_t> position_{0};
    std::array<std::array<std::vector<float>,2>,2> planar_{};
    ASIOCallbacks* callbacks_{nullptr};
    unsigned rate_{0};
    bool initialized_{false},prepared_{false},time_info_{false};
    bool active_[2]{false,false};
    char error_[124]{"Driver not initialized"};
};
class ClassFactory final:public IClassFactory {
public:
    ClassFactory(){g_objects.fetch_add(1);}
    ~ClassFactory(){g_objects.fetch_sub(1);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IClassFactory)){
            *out=static_cast<IClassFactory*>(this);AddRef();return S_OK;
        }return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ref_.fetch_add(1)+1;}
    ULONG STDMETHODCALLTYPE Release() override {
        auto n=ref_.fetch_sub(1)-1;if(!n)delete this;return n;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(outer)return CLASS_E_NOAGGREGATION;
        auto* driver=new(std::nothrow) Driver;
        if(!driver)return E_OUTOFMEMORY;
        const auto result=driver->QueryInterface(iid,out);driver->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL v) override {
        if(v)g_locks.fetch_add(1);else g_locks.fetch_sub(1);return S_OK;
    }
private:
    std::atomic<ULONG> ref_{1};
};
} // namespace
extern "C" BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){g_module=h;DisableThreadLibraryCalls(h);}return TRUE;
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** out){
    if(!out)return E_POINTER;*out=nullptr;
    if(!IsEqualCLSID(clsid,kCLSID))return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory=new(std::nothrow) ClassFactory;
    if(!factory)return E_OUTOFMEMORY;
    auto hr=factory->QueryInterface(iid,out);factory->Release();return hr;
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(){
    return (g_objects.load()==0&&g_locks.load()==0)?S_OK:S_FALSE;
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllRegisterServer(){
    // Explicit regsvr32 only; requires elevation for HKLM.
    wchar_t path[MAX_PATH]{};
    DWORD count=GetModuleFileNameW(g_module,path,MAX_PATH);
    if(!count||count>=MAX_PATH)return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    const wchar_t* clsid_child=L"Software\\Classes\\CLSID\\{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}\\InprocServer32";
    HKEY key=nullptr;
    auto status=RegCreateKeyExW(HKEY_LOCAL_MACHINE,clsid_child,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr);
    if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
    auto hr=reg_text(key,nullptr,path);
    if(SUCCEEDED(hr))hr=reg_text(key,L"ThreadingModel",L"Both");
    RegCloseKey(key);
    if(FAILED(hr))return hr;
    status=RegCreateKeyExW(HKEY_LOCAL_MACHINE,kAsioReg,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr);
    if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
    hr=reg_text(key,L"CLSID",kCLSIDText);
    if(SUCCEEDED(hr))hr=reg_text(key,L"Description",L"Modern Shared Audio experimental");
    RegCloseKey(key);
    return hr;
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer(){
    // Never touches the existing Steinberg driver's registry entries.
    auto a=RegDeleteTreeW(HKEY_LOCAL_MACHINE,kAsioReg);
    auto b=RegDeleteTreeW(HKEY_LOCAL_MACHINE,kClassReg);
    if(a!=ERROR_SUCCESS&&a!=ERROR_FILE_NOT_FOUND)return HRESULT_FROM_WIN32(a);
    if(b!=ERROR_SUCCESS&&b!=ERROR_FILE_NOT_FOUND)return HRESULT_FROM_WIN32(b);
    return S_OK;
}
#endif
