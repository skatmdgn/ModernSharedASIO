// Experimental x64 ASIO 2.3 COM driver. Cubase heavy-VST timing under investigation.
// Requires external Steinberg ASIO SDK common/iasiodrv.h under applicable license.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Objbase.h>
#include <avrt.h>
#include <timeapi.h>
#include "modern_asio/producer_fill_policy.hpp"
#include "modern_asio/asio_callback_timeline.hpp"
#include "modern_asio/asio_block_stamp.hpp"
#include "modern_asio/asio_output_silence.hpp"
#include "modern_asio/asio_double_buffer_handoff.hpp"
#include "modern_asio/realtime_load_telemetry.hpp"
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
#include <cstdint>
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
// ASIO SDK Windows systemTime is derived from timeGetTime (milliseconds).
// A single ASIO block has one latched position/time pair, not a new timestamp
// for each getSamplePosition query in that same block.
std::uint64_t asio_system_clock_ns() noexcept {
    return static_cast<std::uint64_t>(timeGetTime()) * 1000000ULL;
}
void put_text(char* dest,const char* value,std::size_t max) noexcept {
    if(dest && max) std::snprintf(dest,max,"%s",value);
}
HRESULT reg_text(HKEY key,const wchar_t* name,const wchar_t* s) noexcept {
    return HRESULT_FROM_WIN32(RegSetValueExW(key,name,0,REG_SZ,
        reinterpret_cast<const BYTE*>(s),static_cast<DWORD>((std::wcslen(s)+1)*sizeof(wchar_t))));
}

// Optional per-user A/B setting: HKCU\Software\ModernSharedASIO PeriodMs.
// 0 is the Windows default period; absent/invalid override selects 5ms.
std::uint32_t configured_period_ms() noexcept {
    std::uint32_t requested = 5;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\ModernSharedASIO",
                      0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS) {
        DWORD value=0,type=0,bytes=sizeof(value);
        if (RegQueryValueExW(key,L"PeriodMs",nullptr,&type,
                            reinterpret_cast<BYTE*>(&value),&bytes)==ERROR_SUCCESS &&
            type==REG_DWORD && bytes==sizeof(value) &&
            (value==0||value==3||value==5||value==10))
            requested=value;
        RegCloseKey(key);
    }
    return requested;
}

// Keep the proven working two-block FIFO target by default. A one-block
// target is EXPLICITLY experimental and trades glitch tolerance for lower
// queued audio. Registry is only read on start(), never on the audio thread.
std::uint32_t configured_queue_target_blocks() noexcept {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\ModernSharedASIO",
                     0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return 2;
    DWORD value=0,type=0,bytes=sizeof(DWORD);
    const bool valid=
        RegQueryValueExW(key,L"QueueTargetBlocks",nullptr,&type,
                         reinterpret_cast<BYTE*>(&value),&bytes)==ERROR_SUCCESS &&
        type==REG_DWORD && bytes==sizeof(DWORD) && (value==1 || value==2);
    RegCloseKey(key);
    return valid?value:2;
}

// Opt-in startup timing experiment. The proven stable default is OFF.
bool configured_early_producer_wake() noexcept {
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\ModernSharedASIO",
                     0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
    DWORD value=0,type=0,bytes=sizeof(DWORD);
    const bool on=RegQueryValueExW(
        key,L"EarlyProducerWake",nullptr,&type,
        reinterpret_cast<BYTE*>(&value),&bytes)==ERROR_SUCCESS &&
        type==REG_DWORD && bytes==sizeof(DWORD) && value==1;
    RegCloseKey(key);
    return on;
}
bool configured_disable_time_info() noexcept {
    // The physical IAudioClock-to-ASIOTime mapping is not validated for Cubase.
    // Do not advertise ASIO TimeInfo by default until it is calibrated.
    // Explicit per-user EnableTimeInfo=1 opts in to experimental TimeInfo.
    // DisableTimeInfo=1 still takes precedence for compatibility with tests.
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\ModernSharedASIO",
                     0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return true;
    DWORD enable=0,disable=0,type=0,bytes=sizeof(DWORD);
    const bool enabled=
        RegQueryValueExW(key,L"EnableTimeInfo",nullptr,&type,
                         reinterpret_cast<BYTE*>(&enable),&bytes)==ERROR_SUCCESS &&
        type==REG_DWORD && bytes==sizeof(DWORD) && enable==1;
    type=0;bytes=sizeof(DWORD);
    const bool disabled=
        RegQueryValueExW(key,L"DisableTimeInfo",nullptr,&type,
                         reinterpret_cast<BYTE*>(&disable),&bytes)==ERROR_SUCCESS &&
        type==REG_DWORD && bytes==sizeof(DWORD) && disable==1;
    RegCloseKey(key);
    return !enabled||disabled;
}

void keep_max(std::atomic<std::uint64_t>& maximum,std::uint64_t value) noexcept {
    auto earlier=maximum.load(std::memory_order_relaxed);
    while(value>earlier &&
          !maximum.compare_exchange_weak(earlier,value,std::memory_order_relaxed)) {}
}

class Driver final : public IASIO {
public:
    Driver():render_(fifo_, &Driver::request_audio, this, kFrames, true),bridge_(fifo_) {g_objects.fetch_add(1);}
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
        callback_count_.store(0);
        callback_timings_.reset_stopped();
        callback_errors_.store(0);
        over_budget_callbacks_.store(0);
        max_callback_us_.store(0);
        silent_host_blocks_.store(0);
        first_nonzero_block_.store(UINT64_MAX);
        last_nonzero_block_.store(UINT64_MAX);
        nonzero_left_blocks_.store(0);
        nonzero_right_blocks_.store(0);
        invalid_float_blocks_.store(0);
        max_left_peak_ppm_.store(0);
        max_right_peak_ppm_.store(0);
        producer_wakes_.store(0);
        latency_queries_.store(0);
        last_reported_latency_.store(0);
        requested_period_ms_=configured_period_ms();
        early_producer_wake_=configured_early_producer_wake();
        queue_target_frames_=configured_queue_target_blocks()*
            static_cast<std::size_t>(kFrames);
        callback_timeline_.reset();
        callback_stamp_.publish({0,asio_system_clock_ns()});
        buffer_index_=0;
        // Do not queue B output here. ASIO bufferSwitch(0) asks Cubase to
        // prepare A while the OPPOSITE previously prepared B is output.
        // The WASAPI render worker initially primes its own device buffer
        // with silence; producer callback 0 transfers B exactly once.
        // Eagerly queuing B and then also queuing A would violate the
        // ASIO alternating-output contract and duplicate initial audio.
        // The first host bufferSwitch(index=0) reports samplePosition=0.
        // Cubase DSP runs on a dedicated producer worker, not the render worker.
        producer_stop_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        producer_wake_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!producer_stop_event_||!producer_wake_event_){
            stop_producer();return ASE_NoMemory;
        }
        try {producer_thread_=std::thread([this]{producer_loop();});}
        catch(...){stop_producer();return ASE_NoMemory;}
        // Protect buffer state before the audio engine can start rendering.
        playing_.store(true,std::memory_order_release);
        start_wall_ns_=qpc_ns();
        // Experimental early DSP wake. Still uses the producer worker and
        // outputs ONLY the opposite ASIO half, exactly as normal operation.
        // WASAPI has not started yet, so there is no first render deadline.
        if(early_producer_wake_ && producer_wake_event_)
            SetEvent(producer_wake_event_);
        auto hr=render_.start(requested_period_ms_);
        if(FAILED(hr)){
            render_.stop();
            stop_producer();
            playing_.store(false,std::memory_order_release);
            put_text(error_,"WASAPI shared-mode startup failed",sizeof(error_));
            return ASE_HWMalfunction;
        }
        if(render_.device_info().sample_rate!=rate_){
            render_.stop();
            stop_producer();
            playing_.store(false,std::memory_order_release);
            put_text(error_,"Device mix sample rate changed; reload driver",sizeof(error_));
            return ASE_NoClock;
        }
        // Keep the existing post-initialization notification for both modes.
        if(producer_wake_event_)SetEvent(producer_wake_event_);
        return ASE_OK;
    }
    ASIOError stop() override {
        // Joining ourselves from a host callback is forbidden.
        if(render_.on_render_thread() ||
           (producer_thread_.joinable() &&
            producer_thread_.get_id()==std::this_thread::get_id()))
            return ASE_InvalidMode;
        const bool was_playing=playing_.load(std::memory_order_acquire);
        render_.stop();
        stop_producer();
        playing_.store(false,std::memory_order_release);
        if(was_playing) write_stopped_diagnostics();
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
        latency_queries_.fetch_add(1,std::memory_order_relaxed);
        last_reported_latency_.store(*out,std::memory_order_relaxed);
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
        modern_asio::AsioBlockStamp pair{};
        if(!callback_stamp_.read(pair))return ASE_HWMalfunction;
        write64(*samples,pair.frame);
        write64(*clock,pair.system_time_ns);
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
        // Diagnostic switch for real Cubase TimeInfo-vs-legacy A/B tests.
        disable_time_info_=configured_disable_time_info();
        time_info_=!disable_time_info_ && cb->asioMessage && cb->bufferSwitchTimeInfo &&
            cb->asioMessage(kAsioSupportsTimeInfo,0,nullptr,nullptr)==1;
        callback_timeline_.reset();callback_stamp_.publish({0,0});buffer_index_=0;prepared_=true;return ASE_OK;
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
    ASIOError outputReady() override {
        // ASIOOutputReady optimization is NOT implemented. The driver copies
        // host buffers immediately after the bufferSwitch callback returns,
        // and must not advertise the asynchronous output-ready handshake.
        // ASIO SDK 2.3 says return ASE_NotPresent unless this mechanism is
        // supported; ASE_OK allows hosts to defer writing their output buffer.
        return ASE_NotPresent;
    }
private:
    static bool request_audio(void* raw) noexcept {
        // Only signals the worker; no host DSP on WASAPI render thread.
        auto* self=static_cast<Driver*>(raw);
        if(!self->producer_wake_event_)return false;
        const bool signaled=SetEvent(self->producer_wake_event_)!=FALSE;
        if(signaled) self->producer_wakes_.fetch_add(1,std::memory_order_relaxed);
        return signaled;
    }
    void stop_producer() noexcept {
        if(producer_stop_event_)SetEvent(producer_stop_event_);
        if(producer_wake_event_)SetEvent(producer_wake_event_);
        if(producer_thread_.joinable())producer_thread_.join();
        if(producer_wake_event_){
            CloseHandle(producer_wake_event_);producer_wake_event_=nullptr;
        }
        if(producer_stop_event_){
            CloseHandle(producer_stop_event_);producer_stop_event_=nullptr;
        }
    }
    void producer_loop() noexcept {
        DWORD task_index=0;
        HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task_index);
        if(!mmcss)mmcss=AvSetMmThreadCharacteristicsW(L"Audio",&task_index);
        if(mmcss)AvSetMmThreadPriority(mmcss,AVRT_PRIORITY_NORMAL);
        HANDLE events[2]{producer_stop_event_,producer_wake_event_};
        for(;;){
            DWORD state=WaitForMultipleObjects(2,events,FALSE,INFINITE);
            if(state==WAIT_OBJECT_0)break;
            if(state!=WAIT_OBJECT_0+1){
                callback_errors_.fetch_add(1,std::memory_order_relaxed);
                break;
            }
            const auto planned=modern_asio::producer_blocks_to_fill(
                fifo_.available_frames(),fifo_.capacity_frames(),
                static_cast<std::size_t>(kFrames),
                queue_target_frames_,4);
            for(std::size_t i=0;i<planned;++i){
                if(WaitForSingleObject(producer_stop_event_,0)==WAIT_OBJECT_0)
                    break;
                if(!produce_one_block())break;
            }
        }
        if(mmcss)AvRevertMmThreadCharacteristics(mmcss);
    }
    bool produce_one_block() noexcept {
        // Only the ASIO producer thread calls Cubase host callbacks.
        if(!prepared_||!callbacks_)return false;
        const long index=buffer_index_;
        const auto block_frame=callback_timeline_.current();
        const auto block_stamp_ns=asio_system_clock_ns();
        callback_stamp_.publish({block_frame,block_stamp_ns});
        // Before every ASIO callback scrub the reused output half so an
        // empty/deferred Cubase processing pass cannot replay older PCM.
        modern_asio::clear_asio_output_planes(
            active_[0]?planar_[0][index].data():nullptr,
            active_[1]?planar_[1][index].data():nullptr,
            static_cast<std::size_t>(kFrames));
        const auto start_ns=qpc_ns();
        try {
            if(time_info_ && callbacks_->bufferSwitchTimeInfo){
                ASIOTime time{};
                time.timeInfo.speed=1.;
                time.timeInfo.sampleRate=static_cast<double>(rate_);
                time.timeInfo.flags=kSystemTimeValid|kSamplePositionValid|kSampleRateValid;
                write64(time.timeInfo.samplePosition,block_frame);
                write64(time.timeInfo.systemTime,block_stamp_ns);
                callbacks_->bufferSwitchTimeInfo(&time,index,ASIOTrue);
            } else {
                callbacks_->bufferSwitch(index,ASIOTrue);
            }
        } catch (...) {
            callback_errors_.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        const auto end_ns=qpc_ns();
        const auto callback_us=end_ns>=start_ns ? (end_ns-start_ns)/1000 : 0;
        const auto block_ordinal=callback_count_.fetch_add(1,std::memory_order_relaxed);
        keep_max(max_callback_us_,callback_us);
        const auto callback_budget_us=rate_
            ? static_cast<std::uint64_t>(kFrames)*1000000ULL/rate_ : 0ULL;
        callback_timings_.observe(callback_us,callback_budget_us,block_ordinal);
        if(callback_budget_us && callback_us>=callback_budget_us)
            over_budget_callbacks_.fetch_add(1,std::memory_order_relaxed);

        // Compare actual ASIO output data with what the host claims to play.
        // O(512) bounded float reads per callback; atomic counters updated once.
        bool left_has_audio=false,right_has_audio=false,invalid_samples=false;
        float left_peak=0.0f,right_peak=0.0f;
        const float* left=active_[0]?planar_[0][index].data():nullptr;
        const float* right=active_[1]?planar_[1][index].data():nullptr;
        for(long f=0;f<kFrames;++f){
            if(left){
                const float v=left[f];
                if(!std::isfinite(v))invalid_samples=true;
                else {
                    const float a=std::fabs(v);
                    if(a>left_peak)left_peak=a;
                    if(a>0.000001f)left_has_audio=true;
                }
            }
            if(right){
                const float v=right[f];
                if(!std::isfinite(v))invalid_samples=true;
                else {
                    const float a=std::fabs(v);
                    if(a>right_peak)right_peak=a;
                    if(a>0.000001f)right_has_audio=true;
                }
            }
        }
        if(!left_has_audio && !right_has_audio)
            silent_host_blocks_.fetch_add(1,std::memory_order_relaxed);
        else {
            std::uint64_t none=UINT64_MAX;
            first_nonzero_block_.compare_exchange_strong(
                none,block_ordinal,std::memory_order_relaxed);
            last_nonzero_block_.store(block_ordinal,std::memory_order_relaxed);
        }
        if(left_has_audio)nonzero_left_blocks_.fetch_add(1,std::memory_order_relaxed);
        if(right_has_audio)nonzero_right_blocks_.fetch_add(1,std::memory_order_relaxed);
        if(invalid_samples)invalid_float_blocks_.fetch_add(1,std::memory_order_relaxed);
        const auto peak_ppm=[](float peak)->std::uint64_t{
            return static_cast<std::uint64_t>(
                static_cast<double>(std::min(peak,1000.0f))*1000000.0);
        };
        keep_max(max_left_peak_ppm_,peak_ppm(left_peak));
        keep_max(max_right_peak_ppm_,peak_ppm(right_peak));
        // ASIO SDK contract: index is the half the host is NOW filling.
        // The OTHER half is the one hardware must play this period.
        // Never queue the just-written half directly.
        const long output_index=modern_asio::asio_output_half_for_callback(index);
        const auto accepted=bridge_.submit(
            active_[0]?planar_[0][output_index].data():nullptr,
            active_[1]?planar_[1][output_index].data():nullptr,kFrames);
        if(accepted!=kFrames){
            callback_errors_.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        callback_timeline_.complete_callback(static_cast<std::uint32_t>(kFrames));
        // Between callbacks return the next block boundary with its projected
        // timestamp, instead of a timestamp which drifts while frame stays fixed.
        const auto next_stamp=block_stamp_ns +
            static_cast<std::uint64_t>(kFrames)*1000000000ULL/(rate_?rate_:1);
        callback_stamp_.publish({callback_timeline_.current(),next_stamp});
        buffer_index_^=1;
        return true;
    }
    // Best-effort append on the ASIO host control thread after both audio
    // threads have joined. No filesystem access from realtime callbacks.
    void write_stopped_diagnostics() noexcept {
        wchar_t path[MAX_PATH]{};
        const DWORD n=GetTempPathW(MAX_PATH,path);
        if(!n || n>=MAX_PATH-40 ||
           wcscat_s(path,L"ModernSharedASIO-debug.log")!=0) return;
        const HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                      nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) return;
        const auto info=render_.device_info();
        const auto stats=render_.stats();
        const auto timing=callback_timings_.snapshot_stopped();
        char line[2400]{};
        const int bytes=std::snprintf(
            line,sizeof(line),
            "ASIO_STOP rate=%u block_frames=%ld requested_period_ms=%u "
            "engine_period_frames=%u wasapi_buffer_frames=%u callbacks=%llu "
            "max_host_callback_us=%llu callback_over_block_budget=%llu "
            "callback_errors=%llu producer_wakes=%llu wasapi_render_events=%llu "
            "underrun_frames=%llu stream_errors=%llu producer_failures=%llu "
            "last_hr=0x%08lx audio_buffer_target_frames=%llu "
            "asio_time_info=%u latency_queries=%llu last_reported_latency_frames=%ld "
            "disable_time_info=%u host_silent_blocks=%llu host_nonzero_left=%llu "
            "host_nonzero_right=%llu host_invalid_float_blocks=%llu "
            "host_peak_left_ppm=%llu host_peak_right_ppm=%llu "
            "first_nonzero_block=%llu last_nonzero_block=%llu "
            "asiodriver_wall_ms=%llu asio_callback_audio_ms=%llu "
            "wasapi_submitted_frames=%llu remaining_fifo_frames=%llu native_direct_frames=%llu "
            "underrun_events=%llu largest_underrun_event_frames=%llu "
            "first_underrun_render_event=%llu "
            "cb_time_hist_500_1k_2k_4k_8k_12k_20k_us=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu "
            "cb_mean_us=%llu cb_longest_overbudget_streak=%llu "
            "cb_first_overbudget_block=%llu "
            "render_queue_samples=%llu render_queue_min_frames=%llu "
            "render_queue_max_frames=%llu render_queue_below_demand_events=%llu "
            "render_queue_first_low_event=%llu "
            "wasapi_stream_latency_100ns=%llu early_producer_wake=%u\n",
            rate_,kFrames,requested_period_ms_,info.period_frames,info.buffer_frames,
            static_cast<unsigned long long>(callback_count_.load()),
            static_cast<unsigned long long>(max_callback_us_.load()),
            static_cast<unsigned long long>(over_budget_callbacks_.load()),
            static_cast<unsigned long long>(callback_errors_.load()),
            static_cast<unsigned long long>(producer_wakes_.load()),
            static_cast<unsigned long long>(stats.render_events),
            static_cast<unsigned long long>(stats.underrun_frames),
            static_cast<unsigned long long>(stats.errors),
            static_cast<unsigned long long>(stats.producer_failures),
            static_cast<unsigned long>(stats.last_stream_hresult),
            static_cast<unsigned long long>(queue_target_frames_),
            time_info_ ? 1u : 0u,
            static_cast<unsigned long long>(latency_queries_.load()),
            last_reported_latency_.load(),
            disable_time_info_ ? 1u : 0u,
            static_cast<unsigned long long>(silent_host_blocks_.load()),
            static_cast<unsigned long long>(nonzero_left_blocks_.load()),
            static_cast<unsigned long long>(nonzero_right_blocks_.load()),
            static_cast<unsigned long long>(invalid_float_blocks_.load()),
            static_cast<unsigned long long>(max_left_peak_ppm_.load()),
            static_cast<unsigned long long>(max_right_peak_ppm_.load()),
            static_cast<unsigned long long>(first_nonzero_block_.load()),
            static_cast<unsigned long long>(last_nonzero_block_.load()),
            static_cast<unsigned long long>((qpc_ns()>=start_wall_ns_?
                    qpc_ns()-start_wall_ns_:0)/1000000ULL),
            static_cast<unsigned long long>(
                    (callback_count_.load()*static_cast<std::uint64_t>(kFrames)*1000ULL)/
                    (rate_?rate_:1)),
            static_cast<unsigned long long>(stats.submitted_frames),
            static_cast<unsigned long long>(fifo_.available_frames()),
            static_cast<unsigned long long>(stats.native_direct_frames),
            static_cast<unsigned long long>(stats.underrun_events),
            static_cast<unsigned long long>(stats.largest_underrun_event_frames),
            static_cast<unsigned long long>(stats.first_underrun_render_event),
            static_cast<unsigned long long>(timing.buckets[0]),
            static_cast<unsigned long long>(timing.buckets[1]),
            static_cast<unsigned long long>(timing.buckets[2]),
            static_cast<unsigned long long>(timing.buckets[3]),
            static_cast<unsigned long long>(timing.buckets[4]),
            static_cast<unsigned long long>(timing.buckets[5]),
            static_cast<unsigned long long>(timing.buckets[6]),
            static_cast<unsigned long long>(timing.buckets[7]),
            static_cast<unsigned long long>(timing.samples?
                timing.total_us/timing.samples:0ULL),
            static_cast<unsigned long long>(timing.longest_over_budget_streak),
            static_cast<unsigned long long>(timing.first_over_budget_block),
            static_cast<unsigned long long>(stats.render_queue_samples),
            static_cast<unsigned long long>(stats.render_queue_min_frames),
            static_cast<unsigned long long>(stats.render_queue_max_frames),
            static_cast<unsigned long long>(stats.render_queue_below_demand_events),
            static_cast<unsigned long long>(stats.render_queue_first_low_event),
            static_cast<unsigned long long>(info.stream_latency_100ns),
            early_producer_wake_?1u:0u);
        if(bytes>0 && static_cast<std::size_t>(bytes)<sizeof(line)) {
            DWORD written=0;
            WriteFile(file,line,static_cast<DWORD>(bytes),&written,nullptr);
        }
        CloseHandle(file);
    }
    std::atomic<ULONG> ref_{1};
    std::uint32_t requested_period_ms_{5};
    bool early_producer_wake_{false};
    std::size_t queue_target_frames_{2*static_cast<std::size_t>(kFrames)};
    std::atomic<std::uint64_t> callback_count_{0};
    modern_asio::CallbackTimingTracker callback_timings_{};
    std::atomic<std::uint64_t> max_callback_us_{0};
    std::atomic<std::uint64_t> over_budget_callbacks_{0};
    std::atomic<std::uint64_t> producer_wakes_{0};
    std::atomic<std::uint64_t> latency_queries_{0};
    std::atomic<long> last_reported_latency_{0};
    bool disable_time_info_{false};
    std::uint64_t start_wall_ns_{0};
    std::atomic<std::uint64_t> silent_host_blocks_{0};
    std::atomic<std::uint64_t> first_nonzero_block_{UINT64_MAX};
    std::atomic<std::uint64_t> last_nonzero_block_{UINT64_MAX};
    std::atomic<std::uint64_t> nonzero_left_blocks_{0};
    std::atomic<std::uint64_t> nonzero_right_blocks_{0};
    std::atomic<std::uint64_t> invalid_float_blocks_{0};
    std::atomic<std::uint64_t> max_left_peak_ppm_{0};
    std::atomic<std::uint64_t> max_right_peak_ppm_{0};
    modern_asio::SpscStereoRing fifo_{32768};
    modern_asio::WasapiSharedRenderer render_;
    modern_asio::AsioPlaybackBridge bridge_;
    // The host controls start/stop on one thread; producer is SPSC writer.
    HANDLE producer_stop_event_{nullptr};
    HANDLE producer_wake_event_{nullptr};
    std::thread producer_thread_;
    std::atomic<bool> playing_{false};
    long buffer_index_{0};
    std::atomic<std::uint64_t> callback_errors_{0};
    modern_asio::AsioCallbackTimeline callback_timeline_{};
    modern_asio::AsioBlockStampChannel callback_stamp_{};
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
STDAPI DllGetClassObject(REFCLSID clsid,REFIID iid,LPVOID* out){
    if(!out)return E_POINTER;*out=nullptr;
    if(!IsEqualCLSID(clsid,kCLSID))return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory=new(std::nothrow) ClassFactory;
    if(!factory)return E_OUTOFMEMORY;
    auto hr=factory->QueryInterface(iid,out);factory->Release();return hr;
}
STDAPI DllCanUnloadNow(void){
    return (g_objects.load()==0&&g_locks.load()==0)?S_OK:S_FALSE;
}
STDAPI DllRegisterServer(void){
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
STDAPI DllUnregisterServer(void){
    // Never touches the existing Steinberg driver's registry entries.
    auto a=RegDeleteTreeW(HKEY_LOCAL_MACHINE,kAsioReg);
    auto b=RegDeleteTreeW(HKEY_LOCAL_MACHINE,kClassReg);
    if(a!=ERROR_SUCCESS&&a!=ERROR_FILE_NOT_FOUND)return HRESULT_FROM_WIN32(a);
    if(b!=ERROR_SUCCESS&&b!=ERROR_FILE_NOT_FOUND)return HRESULT_FROM_WIN32(b);
    return S_OK;
}
#endif
