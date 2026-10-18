#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace modern_asio {
// Single-producer/single-consumer interleaved stereo float ring.
// Memory preallocated; push/pop do not acquire locks or allocate.
class SpscStereoRing final {
public:
    explicit SpscStereoRing(std::size_t capacity_frames)
        : capacity_(validate_capacity(capacity_frames)), mask_(capacity_ - 1),
          samples_(capacity_ * 2, 0.0f) {}
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

    // Producer-side direct planar-to-interleaved transfer: no temporary
    // intermediate buffer and only ONE publication of the write cursor.
    // The consumer cannot observe an incompletely written stereo block.
    // Mirrors push() semantics on capacity and partial acceptance.
    std::size_t push_planar(const float* left, const float* right,
                            std::size_t frames) noexcept {
        if ((!left && !right) || !frames) return 0;
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        const auto used = static_cast<std::size_t>(write - read);
        assert(used <= capacity_);
        const auto count = std::min(frames, capacity_ - used);
        const auto start = static_cast<std::size_t>(write) & mask_;
        const auto first = std::min(count, capacity_ - start);
        interleave_planar(samples_.data() + start * 2, left, right, first);
        if (count > first)
            interleave_planar(samples_.data(), left ? left + first : nullptr,
                              right ? right + first : nullptr, count - first);
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

    // Use only after both producer and consumer threads have stopped.
    void clear_stopped() noexcept {
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
        std::fill(samples_.begin(), samples_.end(), 0.0f);
    }
    std::size_t available_frames() const noexcept {
        const auto read = read_.load(std::memory_order_acquire);
        const auto write = write_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

private:
    static void interleave_planar(float* dst, const float* left,
                                  const float* right, std::size_t frames) noexcept {
        // Hoist null-channel decisions out of the per-frame loop.
        if (left && right) {
            for (std::size_t i=0; i<frames; ++i) {
                dst[i*2] = left[i];
                dst[i*2+1] = right[i];
            }
        } else if (left) {
            for (std::size_t i=0; i<frames; ++i) {
                dst[i*2] = left[i];
                dst[i*2+1] = 0.0f;
            }
        } else if (right) {
            for (std::size_t i=0; i<frames; ++i) {
                dst[i*2] = 0.0f;
                dst[i*2+1] = right[i];
            }
        }
    }
    static std::size_t validate_capacity(std::size_t n) {
        if (n < 2 || (n & (n - 1)) != 0 ||
            n > std::numeric_limits<std::size_t>::max() / (2*sizeof(float)) ||
            n > std::numeric_limits<std::uint64_t>::max() / 2)
            throw std::invalid_argument("ring capacity must be power of two >=2 without overflow");
        return n;
    }
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<float> samples_;
    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
};
} // namespace modern_asio
