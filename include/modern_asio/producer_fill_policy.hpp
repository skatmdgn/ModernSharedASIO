#pragma once
#include <algorithm>
#include <cstddef>
namespace modern_asio {
// Arithmetic-only FIFO prefill bound; ceil division avoids size_t overflow.
inline std::size_t producer_blocks_to_fill(std::size_t queued,
                                           std::size_t capacity,
                                           std::size_t block_frames,
                                           std::size_t target_frames,
                                           std::size_t maximum_blocks) noexcept {
    if (!block_frames || block_frames>capacity || queued>capacity ||
        maximum_blocks==0) return 0;
    const auto target=std::min(target_frames,capacity);
    if(queued>=target)return 0;
    const auto missing=target-queued;
    const auto needed=missing/block_frames+(missing%block_frames!=0);
    const auto room=(capacity-queued)/block_frames;
    return std::min({needed,room,maximum_blocks});
}
}
