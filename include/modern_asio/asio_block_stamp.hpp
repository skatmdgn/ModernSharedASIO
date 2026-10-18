#pragma once
#include <atomic>
#include <cstdint>

namespace modern_asio {
// Coherent block-aligned ASIO sample-position/system-time pair.
// A single producer publishes; host audio/control threads may read.
// Timestamp units are nanoseconds; the caller supplies the clock source.
struct AsioBlockStamp {
    std::uint64_t frame{0};
    std::uint64_t system_time_ns{0};
};
class AsioBlockStampChannel final {
public:
    void publish(AsioBlockStamp value) noexcept {
        sequence_.fetch_add(1, std::memory_order_acq_rel); // writer active: odd
        frame_.store(value.frame, std::memory_order_relaxed);
        time_ns_.store(value.system_time_ns, std::memory_order_relaxed);
        sequence_.fetch_add(1, std::memory_order_release); // complete: even
    }
    bool read(AsioBlockStamp& result) const noexcept {
        for (unsigned tries=0; tries<128; ++tries) {
            const auto before=sequence_.load(std::memory_order_acquire);
            if (before & 1u) continue;
            const AsioBlockStamp value{
                frame_.load(std::memory_order_relaxed),
                time_ns_.load(std::memory_order_relaxed)
            };
            const auto after=sequence_.load(std::memory_order_acquire);
            if (before==after) { result=value; return true; }
        }
        return false;
    }
private:
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> frame_{0};
    std::atomic<std::uint64_t> time_ns_{0};
};
}
