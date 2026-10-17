#pragma once
#include "modern_asio/spsc_stereo_ring.hpp"
#include <algorithm>
#include <cstddef>

namespace modern_asio {
// Called by the WASAPI render worker only. Each callback generates one ASIO
// output block; bounded by capacity/block to prevent runaway callbacks.
using AudioRequest = bool(*)(void*) noexcept;
struct DemandResult {
    std::size_t available_frames{0};
    std::size_t requests{0};
    bool stalled{false};
};
inline DemandResult fulfill_audio_demand(SpscStereoRing& fifo,
                                         std::size_t requested_frames,
                                         std::size_t block_frames,
                                         AudioRequest request,
                                         void* context,
                                         bool allow_source = true) noexcept {
    DemandResult result{};
    const auto capacity = fifo.capacity_frames();
    const auto target = std::min(requested_frames, capacity);
    if (!allow_source || !request || block_frames == 0 || block_frames > capacity) {
        result.available_frames = fifo.available_frames();
        return result;
    }
    const auto max_requests = capacity / block_frames;
    for (std::size_t i = 0; i < max_requests; ++i) {
        const auto before = fifo.available_frames();
        if (before >= target || capacity - before < block_frames) break;
        ++result.requests;
        if (!request(context)) {result.stalled = true;break;}
        const auto after = fifo.available_frames();
        if (after <= before) {result.stalled = true;break;}
    }
    result.available_frames = fifo.available_frames();
    return result;
}
} // namespace modern_asio
