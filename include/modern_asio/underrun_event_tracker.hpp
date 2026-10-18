#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
namespace modern_asio {
struct UnderrunEventSnapshot {
    std::uint64_t events{0};
    std::uint64_t max_missing_frames{0};
    std::uint64_t first_event{UINT64_MAX};
};
class UnderrunEventTracker final {
public:
    void reset_stopped() noexcept {
        count_.store(0,std::memory_order_relaxed);
        maximum_.store(0,std::memory_order_relaxed);
        first_.store(UINT64_MAX,std::memory_order_relaxed);
    }
    // Exactly one writer (WASAPI render thread), any number of stats readers.
    // Count an event once after summing all missing batches in that event.
    void record(std::uint64_t missing,std::uint64_t event_id) noexcept {
        if(!missing)return;
        const auto current=maximum_.load(std::memory_order_relaxed);
        if(missing>current)maximum_.store(missing,std::memory_order_relaxed);
        if(count_.load(std::memory_order_relaxed)==0)
            first_.store(event_id,std::memory_order_relaxed);
        count_.fetch_add(1,std::memory_order_relaxed);
    }
    UnderrunEventSnapshot snapshot() const noexcept {
        return {count_.load(std::memory_order_relaxed),
                maximum_.load(std::memory_order_relaxed),
                first_.load(std::memory_order_relaxed)};
    }
private:
    std::atomic<std::uint64_t> count_{0},maximum_{0},first_{UINT64_MAX};
};
}
