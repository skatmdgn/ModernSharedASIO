#pragma once
#include "modern_asio/spsc_stereo_ring.hpp"
#include <cstddef>
#include <cstdint>
namespace modern_asio {
// Allocation-free packed ASIO Float32 channel planes -> interleaved stereo queue.
// Only one callback producer; no blocking or heap allocation in submit().
class AsioPlaybackBridge final {
public:
    explicit AsioPlaybackBridge(SpscStereoRing& fifo) noexcept : fifo_(fifo) {}
    std::size_t submit(const float* left, const float* right, std::size_t frames) noexcept {
        if ((!left && !right) || frames == 0) return 0;
        const auto accepted = fifo_.push_planar(left, right, frames);
        dropped_frames_ += frames - accepted;
        return accepted;
    }
    // Producer-thread-only, or after producer is stopped.
    std::uint64_t dropped_frames() const noexcept { return dropped_frames_; }
private:
    SpscStereoRing& fifo_;
    std::uint64_t dropped_frames_{0};
};
}
