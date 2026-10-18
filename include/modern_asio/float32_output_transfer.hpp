#pragma once
#include "modern_asio/spsc_stereo_ring.hpp"
#include <algorithm>
#include <cstddef>
namespace modern_asio {
struct Float32TransferResult {
    std::size_t copied_frames{0};
    std::size_t silence_frames{0};
};
// The destination is an already-acquired WASAPI IEEE float32 stereo buffer.
// Do not call on the producer thread. Large buffers are processed in chunks
// of at most fifo.capacity_frames() by the caller.
inline Float32TransferResult fill_native_stereo_float32(
    SpscStereoRing& fifo,float* destination,std::size_t frames) noexcept {
    if(!destination || !frames)return {0,frames};
    const auto copied=fifo.pop(destination,frames);
    if(copied<frames)
        std::fill_n(destination+copied*2,(frames-copied)*2,0.0f);
    return {copied,frames-copied};
}
}
