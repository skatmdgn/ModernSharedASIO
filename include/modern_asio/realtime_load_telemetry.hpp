#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace modern_asio {

// Only one ASIO producer thread calls observe(). Read/reset only when that
// producer has stopped. No atomic traffic in the host callback for telemetry.
// Bins: <=0.5ms, <=1ms, <=2ms, <=4ms, <=8ms, <=12ms, <=20ms, >20ms.
struct CallbackTimingSnapshot {
    std::array<std::uint64_t,8> buckets{};
    std::uint64_t samples{0};
    std::uint64_t total_us{0};
    std::uint64_t max_us{0};
    std::uint64_t over_budget{0};
    std::uint64_t longest_over_budget_streak{0};
    std::uint64_t first_over_budget_block{std::numeric_limits<std::uint64_t>::max()};
};
class CallbackTimingTracker final {
public:
    void reset_stopped() noexcept { state_={}; consecutive_=0; }
    void observe(std::uint64_t elapsed_us,
                 std::uint64_t budget_us,
                 std::uint64_t block_index) noexcept {
        static constexpr std::array<std::uint64_t,7> limits{
            500,1000,2000,4000,8000,12000,20000};
        std::size_t bucket=0;
        while(bucket<limits.size() && elapsed_us>limits[bucket])++bucket;
        ++state_.buckets[bucket];
        ++state_.samples;
        state_.total_us+=elapsed_us;
        if(elapsed_us>state_.max_us)state_.max_us=elapsed_us;
        if(budget_us && elapsed_us>=budget_us) {
            ++state_.over_budget;
            ++consecutive_;
            if(consecutive_>state_.longest_over_budget_streak)
                state_.longest_over_budget_streak=consecutive_;
            if(state_.first_over_budget_block==
               std::numeric_limits<std::uint64_t>::max())
                state_.first_over_budget_block=block_index;
        } else consecutive_=0;
    }
    CallbackTimingSnapshot snapshot_stopped() const noexcept { return state_; }
private:
    CallbackTimingSnapshot state_{};
    std::uint64_t consecutive_{0};
};

// Single WASAPI render thread writes; external stats readers may be concurrent.
// This is a sampled FIFO occupancy, NOT the true device's output latency.
struct RenderQueueSnapshot {
    std::uint64_t samples{0};
    std::uint64_t minimum_frames{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t maximum_frames{0};
    std::uint64_t below_demand_events{0};
    std::uint64_t first_below_demand_event{std::numeric_limits<std::uint64_t>::max()};
};
class RenderQueueTracker final {
public:
    void reset_stopped() noexcept {
        samples_.store(0,std::memory_order_relaxed);
        minimum_.store(std::numeric_limits<std::uint64_t>::max(),std::memory_order_relaxed);
        maximum_.store(0,std::memory_order_relaxed);
        low_events_.store(0,std::memory_order_relaxed);
        first_low_.store(std::numeric_limits<std::uint64_t>::max(),std::memory_order_relaxed);
    }
    void observe(std::uint64_t available_frames,
                 std::uint64_t demanded_frames,
                 std::uint64_t event_number) noexcept {
        // One writer: no read-modify-write CAS needed, while still providing
        // race-free observations to the control thread.
        const auto low=minimum_.load(std::memory_order_relaxed);
        if(available_frames<low)minimum_.store(available_frames,std::memory_order_relaxed);
        const auto high=maximum_.load(std::memory_order_relaxed);
        if(available_frames>high)maximum_.store(available_frames,std::memory_order_relaxed);
        if(available_frames<demanded_frames) {
            if(low_events_.load(std::memory_order_relaxed)==0)
                first_low_.store(event_number,std::memory_order_relaxed);
            low_events_.fetch_add(1,std::memory_order_relaxed);
        }
        samples_.fetch_add(1,std::memory_order_relaxed);
    }
    RenderQueueSnapshot snapshot() const noexcept {
        return {samples_.load(std::memory_order_relaxed),
                minimum_.load(std::memory_order_relaxed),
                maximum_.load(std::memory_order_relaxed),
                low_events_.load(std::memory_order_relaxed),
                first_low_.load(std::memory_order_relaxed)};
    }
private:
    std::atomic<std::uint64_t> samples_{0};
    std::atomic<std::uint64_t> minimum_{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> maximum_{0};
    std::atomic<std::uint64_t> low_events_{0};
    std::atomic<std::uint64_t> first_low_{std::numeric_limits<std::uint64_t>::max()};
};
} // namespace modern_asio
