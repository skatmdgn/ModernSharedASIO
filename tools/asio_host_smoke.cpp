// Standalone registry-free ASIO output test host.
// This tool does not call DllRegisterServer, regsvr32 or touch the ASIO registry.
// Requires a locally built ModernSharedASIO.dll and the external ASIO SDK.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <iasiodrv.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <vector>

namespace {
constexpr CLSID kDriverCLSID{
    0x6fa63f59,0xa581,0x4d9a,{0xb8,0xcb,0x73,0xc3,0xbd,0x3a,0x6d,0x46}
};
using FactoryProc = HRESULT (STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);

struct HostState {
    ASIOBufferInfo outputs[2]{};
    std::vector<float> waveform{};
    std::size_t cursor{0};
    long block_frames{0};
    bool play_tone{false};
    bool supports_time_info{false};
    std::atomic<unsigned long> callbacks{0};
    std::atomic<unsigned long> invalid_buffers{0};
    std::atomic<unsigned long> bad_sample_positions{0};
    std::atomic<unsigned long> bad_buffer_indices{0};
    std::atomic<unsigned long> first_callback_position{0xFFFFFFFFul};
    IASIO* driver{nullptr};
};
HostState* g_host = nullptr; // Assigned before ASIO start, cleared after stop/join.

void buffer_switch(long half, ASIOBool) {
    auto* host = g_host;
    if (!host || (half != 0 && half != 1)) {
        if (host) host->invalid_buffers.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    const auto ordinal=host->callbacks.load(std::memory_order_relaxed);
    if(half != static_cast<long>(ordinal&1u))
        host->bad_buffer_indices.fetch_add(1,std::memory_order_relaxed);
    if(host->driver) {
        ASIOSamples position{};
        ASIOTimeStamp stamp{};
        if(host->driver->getSamplePosition(&position,&stamp)!=ASE_OK) {
            host->bad_sample_positions.fetch_add(1,std::memory_order_relaxed);
        } else {
            const auto actual=(static_cast<std::uint64_t>(position.hi)<<32)|
                              static_cast<std::uint64_t>(position.lo);
            const auto expected=static_cast<std::uint64_t>(ordinal)*
                                static_cast<std::uint64_t>(host->block_frames);
            if(!ordinal)
                host->first_callback_position.store(
                    static_cast<unsigned long>(position.lo),std::memory_order_relaxed);
            if(actual!=expected)
                host->bad_sample_positions.fetch_add(1,std::memory_order_relaxed);
        }
    }
    auto* left = static_cast<float*>(host->outputs[0].buffers[half]);
    auto* right = static_cast<float*>(host->outputs[1].buffers[half]);
    if (!left || !right || host->waveform.empty()) {
        host->invalid_buffers.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    for (long frame = 0; frame < host->block_frames; ++frame) {
        float value = 0.0f;
        if (host->play_tone) value = host->waveform[host->cursor];
        left[frame] = value;
        right[frame] = value;
        if (++host->cursor >= host->waveform.size()) host->cursor = 0;
    }
    host->callbacks.fetch_add(1,std::memory_order_relaxed);
}
void rate_changed(ASIOSampleRate) {}
long asio_message(long selector, long value, void*, double*) {
    if (selector == kAsioEngineVersion) return 2;
    if (selector == kAsioSelectorSupported)
        return value == kAsioSupportsTimeInfo && g_host &&
               g_host->supports_time_info ? 1 : 0;
    if (selector == kAsioSupportsTimeInfo)
        return g_host && g_host->supports_time_info ? 1 : 0;
    return 0;
}
ASIOTime* time_info(ASIOTime* time, long half, ASIOBool process) {
    auto* host=g_host;
    if(host) {
        const auto ordinal=host->callbacks.load(std::memory_order_relaxed);
        const auto expected=static_cast<std::uint64_t>(ordinal)*
            static_cast<std::uint64_t>(host->block_frames);
        const auto actual=time ? 
            ((static_cast<std::uint64_t>(time->timeInfo.samplePosition.hi)<<32)|
             static_cast<std::uint64_t>(time->timeInfo.samplePosition.lo)) : UINT64_MAX;
        if(actual!=expected)
            host->bad_sample_positions.fetch_add(1,std::memory_order_relaxed);
    }
    buffer_switch(half, process);
    return time;
}
void print_error(const char* stage, long code) {
    std::printf("%s FAILED: %ld\n",stage,code);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 4) {
        std::puts("Usage: modern_asio_host_smoke.exe path-to-ModernSharedASIO.dll [--tone] [--time-info]");
        return 2;
    }
    HostState host;
    for (int i = 2; i < argc; ++i) {
        if (std::wcscmp(argv[i],L"--tone")==0) host.play_tone=true;
        else if (std::wcscmp(argv[i],L"--time-info")==0) host.supports_time_info=true;
        else {std::puts("Unknown argument");return 2;}
    }
    const auto init_hr = CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if (FAILED(init_hr) && init_hr!=RPC_E_CHANGED_MODE) {
        std::printf("CoInitializeEx FAILED: 0x%08lx\n",
                    static_cast<unsigned long>(init_hr));
        return 1;
    }
    auto* module = LoadLibraryW(argv[1]);
    if (!module) {
        std::printf("LoadLibrary FAILED: %lu\n",GetLastError());
        if (SUCCEEDED(init_hr)) CoUninitialize();
        return 1;
    }
    auto* get_factory = reinterpret_cast<FactoryProc>(
        GetProcAddress(module,"DllGetClassObject"));
    IClassFactory* factory = nullptr;
    IASIO* asio = nullptr;
    HRESULT hr = get_factory
        ? get_factory(kDriverCLSID,IID_IClassFactory,
                      reinterpret_cast<void**>(&factory))
        : E_NOINTERFACE;
    if (SUCCEEDED(hr))
        hr = factory->CreateInstance(nullptr,kDriverCLSID,
                                     reinterpret_cast<void**>(&asio));
    bool buffers_created = false;
    bool started = false;
    int exit_code = 1;
    do {
        if (FAILED(hr) || !asio) {
            std::printf("ASIO COM factory FAILED: 0x%08lx\n",
                        static_cast<unsigned long>(hr));
            break;
        }
        if (asio->init(nullptr)!=ASIOTrue) {
            char err[124]{};
            asio->getErrorMessage(err);
            std::printf("IASIO::init FAILED: %s\n",err);
            break;
        }
        const auto output_ready_status=asio->outputReady();
        std::printf("ASIOOutputReady capability=%ld (expected ASE_NotPresent=%ld)\n",
                    static_cast<long>(output_ready_status),
                    static_cast<long>(ASE_NotPresent));
        if(output_ready_status!=ASE_NotPresent) {
            std::puts("Unsupported async output-ready handshake advertised; refusing test");
            break;
        }
        long inputs=0,outputs=0;
        if (asio->getChannels(&inputs,&outputs)!=ASE_OK || outputs<2) {
            std::puts("ASIO stereo output channels unavailable");
            break;
        }
        ASIOSampleRate sample_rate=0;
        if (asio->getSampleRate(&sample_rate)!=ASE_OK ||
            sample_rate<8000.0 || sample_rate>384000.0) {
            std::puts("IASIO::getSampleRate FAILED or invalid rate");
            break;
        }
        long minimum=0,maximum=0,preferred=0,granularity=0;
        if (asio->getBufferSize(&minimum,&maximum,&preferred,&granularity)!=ASE_OK ||
            preferred<16 || preferred>16384) {
            std::puts("IASIO::getBufferSize FAILED");
            break;
        }
        ASIOChannelInfo ch0{},ch1{};
        ch0.channel=0;ch1.channel=1;
        if (asio->getChannelInfo(&ch0)!=ASE_OK ||
            asio->getChannelInfo(&ch1)!=ASE_OK ||
            ch0.type!=ASIOSTFloat32LSB || ch1.type!=ASIOSTFloat32LSB) {
            std::puts("Expected Float32 stereo ASIO output channels");
            break;
        }
        host.block_frames=preferred;
        host.waveform.resize(static_cast<std::size_t>(sample_rate));
        constexpr double kPi=3.14159265358979323846;
        for (std::size_t i=0; i<host.waveform.size(); ++i)
            host.waveform[i]=static_cast<float>(
                0.01 * std::sin(2.0*kPi*440.0 *
                    static_cast<double>(i)/sample_rate));
        host.outputs[0].isInput=ASIOFalse;
        host.outputs[0].channelNum=0;
        host.outputs[1].isInput=ASIOFalse;
        host.outputs[1].channelNum=1;
        ASIOCallbacks callbacks{};
        callbacks.bufferSwitch=&buffer_switch;
        callbacks.sampleRateDidChange=&rate_changed;
        callbacks.asioMessage=&asio_message;
        callbacks.bufferSwitchTimeInfo=&time_info;
        g_host=&host;
        host.driver=asio;
        auto status=asio->createBuffers(host.outputs,2,preferred,&callbacks);
        if (status!=ASE_OK) {
            print_error("IASIO::createBuffers",status);
            break;
        }
        buffers_created=true;
        std::printf("Registry-free ASIO host: rate=%.0f, block=%ld, channels=%ld, requested_mode=%s\n",
                    sample_rate,preferred,outputs,
                    host.supports_time_info ? "bufferSwitchTimeInfo":"bufferSwitch");
        std::puts(host.play_tone ? "Quiet 440Hz ASIO tone for 5 seconds" :
                                   "Silence for 5 seconds");
        status=asio->start();
        if (status!=ASE_OK) {
            print_error("IASIO::start",status);
            break;
        }
        started=true;
        Sleep(5000);
        status=asio->stop();
        started=false;
        if (status!=ASE_OK) {
            print_error("IASIO::stop",status);
            break;
        }
        ASIOSamples samples{};
        ASIOTimeStamp stamp{};
        status=asio->getSamplePosition(&samples,&stamp);
        std::printf("ASIO callbacks=%lu invalid_buffers=%lu sample_position_low=%lu "
                    "first_callback_position=%lu bad_sample_positions=%lu "
                    "bad_buffer_indices=%lu\n",
                    host.callbacks.load(std::memory_order_relaxed),
                    host.invalid_buffers.load(std::memory_order_relaxed),
                    static_cast<unsigned long>(samples.lo),
                    host.first_callback_position.load(std::memory_order_relaxed),
                    host.bad_sample_positions.load(std::memory_order_relaxed),
                    host.bad_buffer_indices.load(std::memory_order_relaxed));
        const auto expected_samples=static_cast<std::uint64_t>(host.callbacks.load())*
            static_cast<std::uint64_t>(preferred);
        const auto actual_samples=(static_cast<std::uint64_t>(samples.hi)<<32)|
            static_cast<std::uint64_t>(samples.lo);
        if (status!=ASE_OK || host.callbacks.load()==0 ||
            host.invalid_buffers.load()!=0 || host.bad_sample_positions.load()!=0 ||
            host.bad_buffer_indices.load()!=0 ||
            host.first_callback_position.load()!=0 ||
            actual_samples!=expected_samples) {
            std::puts("ASIO stream callback validation FAILED");
            break;
        }
        exit_code=0;
    } while (false);
    if (started) asio->stop();
    if (buffers_created && asio->disposeBuffers()!=ASE_OK) {
        std::puts("IASIO::disposeBuffers FAILED");
        exit_code=1;
    }
    g_host=nullptr;
    if (asio) asio->Release();
    if (factory) factory->Release();
    FreeLibrary(module);
    if (SUCCEEDED(init_hr)) CoUninitialize();
    std::printf("Registry-free ASIO host smoke: %s\n",exit_code==0 ? "PASS":"FAIL");
    return exit_code;
}
#endif
