#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "modern_asio/wasapi_shared_renderer.hpp"
#include "modern_asio/period_policy.hpp"
#include "modern_asio/engine_demand.hpp"
#include <audioclient.h>
#include <avrt.h>
#include <cmath>
#include <cstring>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>
#include <limits>

using Microsoft::WRL::ComPtr;
namespace modern_asio {
namespace {
struct CoInit {
    HRESULT hr{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ~CoInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
struct CoTaskFormat {
    WAVEFORMATEX* value{nullptr};
    ~CoTaskFormat() { CoTaskMemFree(value); }
};
struct HandleOwner {
    HANDLE value{nullptr};
    ~HandleOwner() { if (value) CloseHandle(value); }
};
struct MmcssOwner {
    HANDLE value{nullptr};
    ~MmcssOwner() { if (value) AvRevertMmThreadCharacteristics(value); }
};
struct ClientStopOwner {
    IAudioClient* client{nullptr};
    ~ClientStopOwner() { if (client) client->Stop(); }
};

enum class Kind { float32, pcm16, pcm24, pcm32, unsupported };
struct FormatDesc {
    Kind kind{Kind::unsupported};
    unsigned valid_bits{0};
    unsigned channels{0};
    unsigned sample_bytes{0};
    unsigned block_align{0};
};
FormatDesc describe(const WAVEFORMATEX* wave) noexcept {
    FormatDesc f{};
    if (!wave || wave->nChannels == 0 || wave->nChannels > 8 ||
        wave->nSamplesPerSec == 0) return f;
    WORD tag = wave->wFormatTag;
    unsigned valid_bits = wave->wBitsPerSample;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (wave->cbSize < 22) return f;
        const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave);
        if (IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
            tag = WAVE_FORMAT_PCM;
        else return f;
        if (extended->Samples.wValidBitsPerSample) valid_bits = extended->Samples.wValidBitsPerSample;
    }
    if (wave->wBitsPerSample % 8 != 0 || wave->wBitsPerSample == 0) return f;
    const unsigned bytes = wave->wBitsPerSample / 8;
    if (wave->nBlockAlign != wave->nChannels * bytes) return f;
    f.channels = wave->nChannels;
    f.block_align = wave->nBlockAlign;
    f.sample_bytes = bytes;
    f.valid_bits = valid_bits;
    if (tag == WAVE_FORMAT_IEEE_FLOAT && bytes == 4 && valid_bits == 32) f.kind = Kind::float32;
    if (tag == WAVE_FORMAT_PCM && bytes == 2 && valid_bits == 16) f.kind = Kind::pcm16;
    if (tag == WAVE_FORMAT_PCM && bytes == 3 && valid_bits == 24) f.kind = Kind::pcm24;
    if (tag == WAVE_FORMAT_PCM && bytes == 4 && (valid_bits == 24 || valid_bits == 32)) f.kind = Kind::pcm32;
    return f;
}
void convert(float const* input, BYTE* dst, UINT32 frames, const FormatDesc& f) noexcept {
    if (f.kind == Kind::float32 && f.channels == 2) {
        std::memcpy(dst, input, static_cast<std::size_t>(frames) * 2 * sizeof(float));
        return;
    }
    // Extra physical output channels stay silent; stereo is not blindly duplicated.
    for (UINT32 frame = 0; frame < frames; ++frame) {
        for (unsigned c = 0; c < f.channels; ++c) {
            float sample = c < 2 ? input[2 * frame + c] : 0.0f;
            if (!std::isfinite(sample)) sample = 0.0f;
            sample = std::clamp(sample, -1.0f, 1.0f);
            BYTE* out = dst + static_cast<std::size_t>(frame) * f.block_align + c * f.sample_bytes;
            if (f.kind == Kind::float32) {
                std::memcpy(out, &sample, sizeof(sample));
            } else if (f.kind == Kind::pcm16) {
                const auto v = static_cast<std::int16_t>(std::lround(sample * (sample < 0 ? 32768.0f : 32767.0f)));
                std::memcpy(out, &v, sizeof(v));
            } else if (f.kind == Kind::pcm24) {
                const auto v = static_cast<std::int32_t>(std::llround(static_cast<double>(sample) *
                    (sample < 0 ? 8388608.0 : 8388607.0)));
                out[0] = static_cast<BYTE>(v);
                out[1] = static_cast<BYTE>(v >> 8);
                out[2] = static_cast<BYTE>(v >> 16);
            } else if (f.kind == Kind::pcm32) {
                // With 24 valid bits in 32-bit PCM, the low byte is padding.
                const double neg = f.valid_bits == 24 ? 8388608.0 : 2147483648.0;
                const double pos = f.valid_bits == 24 ? 8388607.0 : 2147483647.0;
                const auto raw = std::llround(static_cast<double>(sample) *
                                              (sample < 0 ? neg : pos));
                const auto value = static_cast<std::int32_t>(
                    raw * (f.valid_bits == 24 ? 256LL : 1LL));
                std::memcpy(out, &value, sizeof(value));
            }
        }
    }
}
} // namespace

HRESULT WasapiSharedRenderer::start(std::uint32_t target_period_ms) {
    if (target_period_ms > 20) return E_INVALIDARG;
    if (thread_.joinable()) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) return HRESULT_FROM_WIN32(GetLastError());
    {
        std::lock_guard lock(mutex_);
        ready_ = false;
        init_result_ = E_UNEXPECTED;
        info_ = {};
    }
    render_events_.store(0);
    underrun_frames_.store(0);
    errors_.store(0);
    producer_failures_.store(0);
    clock_reads_.store(0);
    clock_device_position_.store(0);
    clock_qpc_100ns_.store(0);
    clock_frequency_.store(0);
    submitted_frames_.store(0);
    last_stream_hresult_.store(S_OK);
    coherent_clock_.reset_stopped();
    try {
        thread_ = std::thread([this, target_period_ms] { thread_main(target_period_ms); });
    } catch (...) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
        return E_OUTOFMEMORY;
    }
    std::unique_lock lock(mutex_);
    ready_cv_.wait(lock, [this]{return ready_;});
    const HRESULT hr = init_result_;
    lock.unlock();
    if (FAILED(hr)) stop();
    return hr;
}

bool WasapiSharedRenderer::on_render_thread() const noexcept {
    return thread_.joinable() && thread_.get_id() == std::this_thread::get_id();
}
void WasapiSharedRenderer::stop() noexcept {
    // The worker cannot join itself. On the render worker, only signal shutdown
    // and let the owning control thread perform the later join and cleanup.
    if (stop_event_) SetEvent(stop_event_);
    if (on_render_thread()) return;
    if (thread_.joinable()) thread_.join();
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
}
OutputInfo WasapiSharedRenderer::device_info() const noexcept {
    std::lock_guard lock(mutex_);
    return info_;
}
OutputStats WasapiSharedRenderer::stats() const noexcept {
    return {render_events_.load(), underrun_frames_.load(), errors_.load(),
            producer_failures_.load(),clock_reads_.load(),clock_device_position_.load(),
            clock_qpc_100ns_.load(),clock_frequency_.load(),
            submitted_frames_.load(),last_stream_hresult_.load()};
}
bool WasapiSharedRenderer::read_clock_observation(ClockObservation& out) const noexcept {
    return coherent_clock_.read(out);
}
void WasapiSharedRenderer::publish_initialized(HRESULT hr, OutputInfo info) noexcept {
    {
        std::lock_guard lock(mutex_);
        init_result_ = hr;
        info_ = info;
        ready_ = true;
    }
    ready_cv_.notify_one();
}
void WasapiSharedRenderer::thread_main(std::uint32_t target_ms) noexcept {
    CoInit com;
    if (FAILED(com.hr)) { publish_initialized(com.hr); return; }
    ComPtr<IMMDeviceEnumerator> devices;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(devices.GetAddressOf()));
    if (FAILED(hr)) { publish_initialized(hr); return; }
    ComPtr<IMMDevice> endpoint;
    hr = devices->GetDefaultAudioEndpoint(eRender, eConsole, endpoint.GetAddressOf());
    if (FAILED(hr)) { publish_initialized(hr); return; }
    ComPtr<IAudioClient> client;
    hr = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER,
                            nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) { publish_initialized(hr); return; }
    CoTaskFormat mix;
    hr = client->GetMixFormat(&mix.value);
    if (FAILED(hr)) { publish_initialized(hr); return; }
    const auto fmt = describe(mix.value);
    if (fmt.kind == Kind::unsupported) { publish_initialized(AUDCLNT_E_UNSUPPORTED_FORMAT); return; }

    OutputInfo info{};
    info.sample_rate = mix.value->nSamplesPerSec;
    info.channels = mix.value->nChannels;
    ComPtr<IAudioClient3> modern;
    if (SUCCEEDED(client.As(&modern))) {
        UINT32 d=0, f=0, min=0, max=0;
        if (SUCCEEDED(modern->GetSharedModeEnginePeriod(mix.value, &d, &f, &min, &max))) {
            const auto period = choose_shared_period({d,f,min,max}, info.sample_rate, target_ms);
            if (period && SUCCEEDED(modern->InitializeSharedAudioStream(
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, mix.value, nullptr))) {
                info.iaudio_client3 = true;
                info.period_frames = period;
            }
        }
    }
    if (!info.iaudio_client3) {
        // Re-activate a fresh IAudioClient; an earlier init could have changed state.
        modern.Reset();
        client.Reset();
        hr = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER,
                                nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
        if (FAILED(hr)) { publish_initialized(hr); return; }
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, mix.value, nullptr);
        if (FAILED(hr)) { publish_initialized(hr); return; }
    }

    hr = client->GetBufferSize(&info.buffer_frames);
    if (FAILED(hr) || info.buffer_frames == 0) {
        publish_initialized(FAILED(hr) ? hr : E_FAIL); return;
    }
    REFERENCE_TIME latency = 0;
    if (SUCCEEDED(client->GetStreamLatency(&latency)) && latency > 0)
        info.stream_latency_100ns = static_cast<std::uint64_t>(latency);
    HandleOwner audio_event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if (!audio_event.value) { publish_initialized(HRESULT_FROM_WIN32(GetLastError())); return; }
    hr = client->SetEventHandle(audio_event.value);
    if (FAILED(hr)) { publish_initialized(hr); return; }
    ComPtr<IAudioRenderClient> render;
    hr = client->GetService(IID_PPV_ARGS(render.GetAddressOf()));
    if (FAILED(hr)) { publish_initialized(hr); return; }
    // Clock telemetry is optional; output still works without IAudioClock.
    ComPtr<IAudioClock> audio_clock;
    UINT64 hardware_frequency = 0;
    if (SUCCEEDED(client->GetService(IID_PPV_ARGS(audio_clock.GetAddressOf())))) {
        if (FAILED(audio_clock->GetFrequency(&hardware_frequency)) || !hardware_frequency)
            audio_clock.Reset();
    }
    clock_frequency_.store(hardware_frequency, std::memory_order_relaxed);
    unsigned clock_sample_counter = 0;
    // Preallocate before the realtime loop; no allocations in render events.
    std::vector<float> scratch;
    try { scratch.resize(static_cast<std::size_t>(info.buffer_frames) * 2); }
    catch (...) { publish_initialized(E_OUTOFMEMORY); return; }

    // Startup priming must not synchronously call the Cubase host.
    auto render_available = [&](bool allow_host_callback) -> HRESULT {
        UINT32 padding = 0;
        auto result = client->GetCurrentPadding(&padding);
        if (FAILED(result)) return result;
        if (padding > info.buffer_frames) return E_UNEXPECTED;
        const UINT32 frames = info.buffer_frames - padding;
        if (!frames) return S_OK;
        // Device position units are clock-frequency ticks; QPC is 100ns.
        if (audio_clock && (++clock_sample_counter % 8u) == 0u) {
            UINT64 ticks = 0, qpc100ns = 0;
            if (audio_clock->GetPosition(&ticks, &qpc100ns)==S_OK && qpc100ns!=0) {
                clock_device_position_.store(ticks, std::memory_order_relaxed);
                clock_qpc_100ns_.store(qpc100ns, std::memory_order_relaxed);
                coherent_clock_.publish({ticks,hardware_frequency,qpc100ns});
                clock_reads_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Callback production is paced by actual WASAPI render-buffer demand,
        // not by an independent std::chrono timer. Do this before GetBuffer.
        UINT32 remaining = frames;
        UINT32 offset = 0;
        const auto capacity = static_cast<UINT32>(
            std::min<std::size_t>(queue_.capacity_frames(), UINT32_MAX));
        while (remaining) {
            const auto batch = std::min(remaining, capacity);
            if (!asynchronous_source_) {
                // Legacy/synchronous mode: useful for simple diagnostics only.
                const auto demand = fulfill_audio_demand(
                    queue_, batch, source_block_frames_, source_, source_context_,
                    allow_host_callback);
                if (demand.stalled)
                    producer_failures_.fetch_add(1,std::memory_order_relaxed);
            }
            // Async source: NEVER call the Cubase DSP callback on this worker.
            const auto got = queue_.pop(scratch.data() + static_cast<std::size_t>(offset) * 2, batch);
            if (got < batch) {
                std::memset(scratch.data() + (static_cast<std::size_t>(offset) + got) * 2,
                            0, static_cast<std::size_t>(batch - got) * 2 * sizeof(float));
                // Initial startup silence is intentional, not a device underrun.
                if (allow_host_callback)
                    underrun_frames_.fetch_add(batch - got, std::memory_order_relaxed);
            }
            offset += batch;
            remaining -= batch;
        }
        BYTE* dest = nullptr;
        result = render->GetBuffer(frames, &dest);
        if (FAILED(result)) return result;
        convert(scratch.data(), dest, frames, fmt);
        const auto release_hr=render->ReleaseBuffer(frames,0);
        if(SUCCEEDED(release_hr))
            submitted_frames_.fetch_add(frames,std::memory_order_relaxed);
        // After release, wake the separate producer for upcoming event(s).
        // On the WASAPI worker this performs only SetEvent, never host DSP.
        if (SUCCEEDED(release_hr) && allow_host_callback && asynchronous_source_ &&
            source_ && !source_(source_context_))
            producer_failures_.fetch_add(1,std::memory_order_relaxed);
        return release_hr;
    };

    // Apply MMCSS before any host callback on the pre-roll path.
    DWORD avrt_index = 0;
    MmcssOwner mmcss{AvSetMmThreadCharacteristicsW(L"Pro Audio", &avrt_index)};
    if (!mmcss.value) mmcss.value = AvSetMmThreadCharacteristicsW(L"Audio", &avrt_index);
    if (mmcss.value) AvSetMmThreadPriority(mmcss.value, AVRT_PRIORITY_HIGH);
    hr = render_available(false); // prime with prefilled ASIO B half and silence
    if (FAILED(hr)) { publish_initialized(hr); return; }
    hr = client->Start();
    if (FAILED(hr)) { publish_initialized(hr); return; }
    ClientStopOwner stop_client{client.Get()};
    publish_initialized(S_OK, info);

    HANDLE events[2]{stop_event_, audio_event.value};
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 2000);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            render_events_.fetch_add(1, std::memory_order_relaxed);
            hr = render_available(true);
            if (FAILED(hr)) {
                last_stream_hresult_.store(hr,std::memory_order_relaxed);
                errors_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        } else if (wait == WAIT_TIMEOUT) {
            last_stream_hresult_.store(HRESULT_FROM_WIN32(ERROR_TIMEOUT),std::memory_order_relaxed);
            errors_.fetch_add(1,std::memory_order_relaxed);
            break;
        } else {
            const auto error = wait==WAIT_FAILED
                ? HRESULT_FROM_WIN32(GetLastError()) : E_UNEXPECTED;
            last_stream_hresult_.store(error,std::memory_order_relaxed);
            errors_.fetch_add(1,std::memory_order_relaxed);
            break;
        }
    }
    // Stop and all COM/handle teardown finish before thread exit.
}
} // namespace modern_asio
#endif
