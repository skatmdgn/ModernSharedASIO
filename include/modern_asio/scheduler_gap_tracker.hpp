#pragma once
#include <atomic>
#include <cstdint>
#include "modern_asio/qpc_time_math.hpp"

namespace modern_asio {
// Optional, low-frequency scheduler diagnostics. One writer per tracker
// (producer wake thread OR WASAPI render thread); readers may inspect stats
// concurrently. These are scheduling INTERVALS, not DPC attribution or audio
// round-trip latency. Off by default; no QPC probes when disabled.
struct SchedulerGapSnapshot {
    std::uint64_t samples{0};
    std::uint64_t total_us{0};
    std::uint64_t maximum_us{0};
    std::uint64_t over_threshold{0};
    std::uint64_t first_over_threshold_ordinal{UINT64_MAX};
};
class SchedulerGapTracker final {
public:
    void reset_stopped() noexcept {
        samples_.store(0,std::memory_order_relaxed);
        total_.store(0,std::memory_order_relaxed);
        maximum_.store(0,std::memory_order_relaxed);
        over_.store(0,std::memory_order_relaxed);
        first_.store(UINT64_MAX,std::memory_order_relaxed);
    }
    // start and end are monotonic QPC ticks. A zero or inverted timestamp
    // denotes an unobserved / coalesced request and is deliberately skipped.
    void observe_ticks(std::uint64_t start,std::uint64_t end,
                       std::uint64_t frequency,std::uint64_t threshold_us,
                       std::uint64_t ordinal) noexcept {
        if(!start || end<start || !frequency)return;
        observe_microseconds(qpc_elapsed_us(start,end,frequency),
                            threshold_us,ordinal);
    }
    void observe_microseconds(std::uint64_t elapsed,std::uint64_t threshold,
                              std::uint64_t ordinal) noexcept {
        // Single writer: relaxed atomics are sufficient for tear-free
        // asynchronous snapshots without CAS loops or locks.
        if(elapsed>maximum_.load(std::memory_order_relaxed))
            maximum_.store(elapsed,std::memory_order_relaxed);
        total_.fetch_add(elapsed,std::memory_order_relaxed);
        if(threshold && elapsed>threshold) {
            if(over_.load(std::memory_order_relaxed)==0)
                first_.store(ordinal,std::memory_order_relaxed);
            over_.fetch_add(1,std::memory_order_relaxed);
        }
        samples_.fetch_add(1,std::memory_order_relaxed);
    }
    SchedulerGapSnapshot snapshot() const noexcept {
        return {samples_.load(std::memory_order_relaxed),
                total_.load(std::memory_order_relaxed),
                maximum_.load(std::memory_order_relaxed),
                over_.load(std::memory_order_relaxed),
                first_.load(std::memory_order_relaxed)};
    }
private:
    std::atomic<std::uint64_t> samples_{0},total_{0},maximum_{0},over_{0};
    std::atomic<std::uint64_t> first_{UINT64_MAX};
};
} // namespace modern_asio
