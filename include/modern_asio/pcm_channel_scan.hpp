#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#if defined(__SSE2__) || defined(_M_X64)
# include <emmintrin.h>
# define MODERN_ASIO_PCM_HAS_SSE2 1
#else
# define MODERN_ASIO_PCM_HAS_SSE2 0
#endif
namespace modern_asio {
// Identical scalar PCM semantics: nonzero >1e-6f, invalid NaN/Inf,
// max absolute magnitude of finite samples only. No sample data changes.
// x64 uses universally available SSE2; non-x64 retains portable scalar code.
struct ChannelScan { bool nonzero{false}; bool invalid{false}; float peak{0.0f}; };
inline ChannelScan scan_pcm_channel(const float* data,std::size_t frames) noexcept {
    ChannelScan out{};
    if(!data)return out;
    std::size_t i=0;
#if MODERN_ASIO_PCM_HAS_SSE2
    const auto signMask=_mm_set1_ps(-0.0f);
    // Bitwise IEEE754 exponent classification avoids an FP invalid exception
    // when a DAW/VST leaves exception traps enabled and sends NaN/Infinity.
    const auto exponentMask=_mm_set1_epi32(0x7f800000);
    const auto threshold=_mm_set1_ps(0.000001f);
    auto maxima=_mm_setzero_ps();
    for(;i+4<=frames;i+=4) {
        const auto values=_mm_loadu_ps(data+i);
        const auto magnitudes=_mm_andnot_ps(signMask,values);
        const auto exponent=_mm_and_si128(_mm_castps_si128(values),exponentMask);
        const auto special=_mm_cmpeq_epi32(exponent,exponentMask);
        const auto invalidMask=_mm_castsi128_ps(special);
        if(_mm_movemask_ps(invalidMask))out.invalid=true;
        const auto finiteMagnitudes=_mm_andnot_ps(invalidMask,magnitudes);
        if(_mm_movemask_ps(_mm_cmpgt_ps(finiteMagnitudes,threshold)))
            out.nonzero=true;
        maxima=_mm_max_ps(maxima,finiteMagnitudes);
    }
    alignas(16) float lanes[4];
    _mm_store_ps(lanes,maxima);
    out.peak=std::max(std::max(lanes[0],lanes[1]),
                      std::max(lanes[2],lanes[3]));
#endif
    for(;i<frames;++i) {
        const auto value=data[i];
        if(!std::isfinite(value))out.invalid=true;
        else {
            const auto mag=std::fabs(value);
            if(mag>out.peak)out.peak=mag;
            if(mag>0.000001f)out.nonzero=true;
        }
    }
    return out;
}
} // namespace modern_asio
