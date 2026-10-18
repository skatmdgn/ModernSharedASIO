#pragma once
#include <atomic>
#include <cstdint>

namespace modern_asio {
// ASIO output sample positions describe host callback blocks, not the count of
// device frames prefilled by the driver. The very first bufferSwitch corresponds
// to position 0, including when ASIO output buffer B was filled before start().
class AsioCallbackTimeline final {
public:
    void reset() noexcept { next_callback_frame_.store(0, std::memory_order_release); }
    std::uint64_t current() const noexcept {
        return next_callback_frame_.load(std::memory_order_acquire);
    }
    void complete_callback(std::uint32_t block_frames) noexcept {
        next_callback_frame_.fetch_add(block_frames, std::memory_order_release);
    }
private:
    std::atomic<std::uint64_t> next_callback_frame_{0};
};
}
