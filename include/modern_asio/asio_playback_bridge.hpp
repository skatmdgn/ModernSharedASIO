#pragma once
#include "modern_asio/spsc_stereo_ring.hpp"
#include <algorithm>
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
        std::size_t sent = 0;
        float temporary[256]; // 128 interleaved frames; bounded stack use
        while (sent < frames) {
            const auto count = std::min<std::size_t>(128, frames - sent);
            for (std::size_t i = 0; i < count; ++i) {
                temporary[i * 2] = left ? left[sent + i] : 0.0f;
                temporary[i * 2 + 1] = right ? right[sent + i] : 0.0f;
            }
            const auto pushed = fifo_.push(temporary, count);
            sent += pushed;
            if (pushed != count) break; // drop overflow; do not block callback
        }
        dropped_frames_ += frames - sent;
        return sent;
    }
    // Producer-thread-only, or after producer is stopped.
    std::uint64_t dropped_frames() const noexcept { return dropped_frames_; }
private:
    SpscStereoRing& fifo_;
    std::uint64_t dropped_frames_{0};
};
}
