#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace modern_asio {
// Single-producer/single-consumer interleaved stereo float ring.
// Memory preallocated; push/pop do not acquire locks or allocate.
class SpscStereoRing final {
public:
    explicit SpscStereoRing(std::size_t capacity_frames)
        : capacity_(capacity_frames), mask_(capacity_frames - 1),
          samples_(capacity_frames * 2, 0.0f) {
        if (capacity_frames < 2 || (capacity_frames & (capacity_frames - 1)) != 0)
            throw std::invalid_argument("ring capacity must be a power of two >= 2");
    }
    SpscStereoRing(const SpscStereoRing&) = delete;
    SpscStereoRing& operator=(const SpscStereoRing&) = delete;

    std::size_t capacity_frames() const noexcept { return capacity_; }

    std::size_t push(const float* stereo, std::size_t frames) noexcept {
        if (!stereo || !frames) return 0;
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        const auto used = static_cast<std::size_t>(write - read);
        assert(used <= capacity_);
        const auto count = std::min(frames, capacity_ - used);
        const auto start = static_cast<std::size_t>(write) & mask_;
        const auto first = std::min(count, capacity_ - start);
        std::memcpy(samples_.data() + start * 2, stereo, first * 2 * sizeof(float));
        if (count > first)
            std::memcpy(samples_.data(), stereo + first * 2, (count - first) * 2 * sizeof(float));
        write_.store(write + count, std::memory_order_release);
        return count;
    }

    std::size_t pop(float* stereo, std::size_t frames) noexcept {
        if (!stereo || !frames) return 0;
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        const auto available = static_cast<std::size_t>(write - read);
        assert(available <= capacity_);
        const auto count = std::min(frames, available);
        const auto start = static_cast<std::size_t>(read) & mask_;
        const auto first = std::min(count, capacity_ - start);
        std::memcpy(stereo, samples_.data() + start * 2, first * 2 * sizeof(float));
        if (count > first)
            std::memcpy(stereo + first * 2, samples_.data(), (count - first) * 2 * sizeof(float));
        read_.store(read + count, std::memory_order_release);
        return count;
    }

    std::size_t available_frames() const noexcept {
        const auto read = read_.load(std::memory_order_acquire);
        const auto write = write_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<float> samples_;
    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
};
} // namespace modern_asio
