#pragma once
#include <algorithm>
#include <cstddef>

namespace modern_asio {
// The ASIO host owns output samples for its callback, but a silent or skipped
// callback may not rewrite every sample. Never replay stale data from a reused
// output half when rendering resumes or transport seeks.
inline void clear_asio_output_planes(float* left, float* right,
                                      std::size_t frames) noexcept {
    if(left) std::fill_n(left,frames,0.0f);
    if(right) std::fill_n(right,frames,0.0f);
}
}
