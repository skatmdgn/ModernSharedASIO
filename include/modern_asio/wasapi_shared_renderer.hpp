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
#include "modern_asio/engine_demand.hpp"
#include "modern_asio/clock_observation.hpp"
#include "modern_asio/underrun_event_tracker.hpp"

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
    std::uint64_t producer_failures{0};
    std::uint64_t clock_reads{0};
    std::uint64_t clock_device_position{0};
    std::uint64_t clock_qpc_100ns{0};
    std::uint64_t clock_frequency{0};
    std::uint64_t submitted_frames{0};
    std::uint64_t native_direct_frames{0};
    std::uint64_t underrun_events{0};
    std::uint64_t largest_underrun_event_frames{0};
    std::uint64_t first_underrun_render_event{UINT64_MAX};
    HRESULT last_stream_hresult{S_OK};
};

// One owning control thread calls start()/stop(). Audio COM interfaces live only
// on the dedicated render worker. A timed-out join is never followed by freeing
// resources which that thread could still reference.
class WasapiSharedRenderer final {
public:
    explicit WasapiSharedRenderer(SpscStereoRing& queue,
                                 AudioRequest source = nullptr,
                                 void* context = nullptr,
                                 std::uint32_t source_block_frames = 0,
                                 bool asynchronous_source = false) noexcept
        : queue_(queue), source_(source), source_context_(context),
          source_block_frames_(source_block_frames),
          asynchronous_source_(asynchronous_source) {}
    ~WasapiSharedRenderer() { stop(); }
    WasapiSharedRenderer(const WasapiSharedRenderer&) = delete;
    WasapiSharedRenderer& operator=(const WasapiSharedRenderer&) = delete;

    // target_period_ms=0 selects Windows default; 3 or 5 requests a smaller period.
    // NEVER acquires WASAPI exclusive mode.
    HRESULT start(std::uint32_t target_period_ms = 0);
    void stop() noexcept;
    // Only the render worker calls the source; never call stop() from this thread.
    bool on_render_thread() const noexcept;
    OutputInfo device_info() const noexcept;
    OutputStats stats() const noexcept;
    bool read_clock_observation(ClockObservation& out) const noexcept;

private:
    void thread_main(std::uint32_t period_ms) noexcept;
    void publish_initialized(HRESULT hr, OutputInfo info = {}) noexcept;
    SpscStereoRing& queue_;
    AudioRequest source_{nullptr};
    void* source_context_{nullptr};
    std::uint32_t source_block_frames_{0};
    bool asynchronous_source_{false};
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
    std::atomic<std::uint64_t> producer_failures_{0};
    std::atomic<std::uint64_t> clock_reads_{0};
    std::atomic<std::uint64_t> clock_device_position_{0};
    std::atomic<std::uint64_t> clock_qpc_100ns_{0};
    std::atomic<std::uint64_t> clock_frequency_{0};
    std::atomic<std::uint64_t> submitted_frames_{0};
    std::atomic<std::uint64_t> native_direct_frames_{0};
    UnderrunEventTracker underrun_tracker_{};
    std::atomic<HRESULT> last_stream_hresult_{S_OK};
    ClockObservationChannel coherent_clock_{};
};
} // namespace modern_asio
#endif
