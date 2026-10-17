#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include "modern_asio/spsc_stereo_ring.hpp"

namespace modern_asio {
struct OutputInfo {
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::uint32_t buffer_frames{0};
    std::uint32_t period_frames{0};
    std::uint64_t stream_latency_100ns{0};
    bool iaudio_client3{false};
};
struct OutputStats {
    std::uint64_t render_events{0};
    std::uint64_t underrun_frames{0};
    std::uint64_t errors{0};
};

// One owning control thread calls start()/stop(). Audio COM interfaces live only
// on the dedicated render worker. A timed-out join is never followed by freeing
// resources which that thread could still reference.
class WasapiSharedRenderer final {
public:
    explicit WasapiSharedRenderer(SpscStereoRing& queue) noexcept : queue_(queue) {}
    ~WasapiSharedRenderer() { stop(); }
    WasapiSharedRenderer(const WasapiSharedRenderer&) = delete;
    WasapiSharedRenderer& operator=(const WasapiSharedRenderer&) = delete;

    // target_period_ms=0 selects Windows default; 3 or 5 requests a smaller period.
    // NEVER acquires WASAPI exclusive mode.
    HRESULT start(std::uint32_t target_period_ms = 0);
    void stop() noexcept;
    OutputInfo device_info() const noexcept;
    OutputStats stats() const noexcept;

private:
    void thread_main(std::uint32_t period_ms) noexcept;
    void publish_initialized(HRESULT hr, OutputInfo info = {}) noexcept;
    SpscStereoRing& queue_;
    HANDLE stop_event_{nullptr};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable ready_cv_;
    bool ready_{false};
    HRESULT init_result_{E_UNEXPECTED};
    OutputInfo info_{};
    std::atomic<std::uint64_t> render_events_{0};
    std::atomic<std::uint64_t> underrun_frames_{0};
    std::atomic<std::uint64_t> errors_{0};
};
} // namespace modern_asio
#endif
