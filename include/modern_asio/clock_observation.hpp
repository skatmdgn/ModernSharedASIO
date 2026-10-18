#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace modern_asio {
// IAudioClock::GetPosition paired device clock and QPC reading.
// QPC is already in 100-nanosecond units, NOT raw performance-counter ticks.
struct ClockObservation {
    std::uint64_t device_ticks{0};
    std::uint64_t device_frequency{0};
    std::uint64_t qpc_100ns{0};
};
inline bool device_ticks_to_frames(const ClockObservation& sample,
                                    std::uint32_t rate,
                                    std::uint64_t& frames) noexcept {
    if (!sample.device_frequency || !rate) return false;
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    const auto seconds = sample.device_ticks / sample.device_frequency;
    const auto fractional = sample.device_ticks % sample.device_frequency;
    if (seconds > max / rate || fractional > max / rate) return false;
    const auto whole = seconds * rate;
    const auto part = (fractional * rate) / sample.device_frequency;
    if (whole > max - part) return false;
    frames = whole + part;
    return true;
}
inline bool qpc_to_asio_nanoseconds(std::uint64_t qpc100ns,
                                     std::uint64_t& ns) noexcept {
    if (qpc100ns > std::numeric_limits<std::uint64_t>::max() / 100) return false;
    ns = qpc100ns * 100;
    return true;
}
// One render-thread writer and arbitrarily many readers. Atomic values and
// a sequence counter prevent mixed clock position / QPC observations.
class ClockObservationChannel final {
public:
    void publish(ClockObservation s) noexcept {
        sequence_.fetch_add(1, std::memory_order_acq_rel);
        ticks_.store(s.device_ticks, std::memory_order_relaxed);
        frequency_.store(s.device_frequency, std::memory_order_relaxed);
        qpc_.store(s.qpc_100ns, std::memory_order_relaxed);
        sequence_.fetch_add(1, std::memory_order_release);
    }
    bool read(ClockObservation& s) const noexcept {
        for(unsigned attempt=0;attempt<8;++attempt){
            const auto begin=sequence_.load(std::memory_order_acquire);
            if(begin & 1U)continue;
            ClockObservation observed{
                ticks_.load(std::memory_order_relaxed),
                frequency_.load(std::memory_order_relaxed),
                qpc_.load(std::memory_order_relaxed)
            };
            const auto end=sequence_.load(std::memory_order_acquire);
            if(begin==end){s=observed;return s.device_frequency!=0;}
        }
        return false;
    }
    void reset_stopped() noexcept { publish({}); }
private:
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> ticks_{0};
    std::atomic<std::uint64_t> frequency_{0};
    std::atomic<std::uint64_t> qpc_{0};
};
}
