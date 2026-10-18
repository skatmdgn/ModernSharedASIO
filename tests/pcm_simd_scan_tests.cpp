#include "modern_asio/pcm_channel_scan.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <random>
#if MODERN_ASIO_PCM_HAS_SSE2
# include <emmintrin.h>
#endif
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
static modern_asio::ChannelScan scalar(const float* v,std::size_t n) {
    modern_asio::ChannelScan out{};
    if(!v)return out;
    for(std::size_t i=0;i<n;++i) {
        if(!std::isfinite(v[i]))out.invalid=true;
        else {
            const float abs=std::fabs(v[i]);
            if(abs>out.peak)out.peak=abs;
            if(abs>0.000001f)out.nonzero=true;
        }
    }
    return out;
}
int main() {
    using modern_asio::scan_pcm_channel;
    std::array<float,520> a{};
    std::mt19937_64 rng(3312);
    for(int trial=0;trial<50000;++trial) {
        const auto n=static_cast<std::size_t>(rng()%513);
        for(std::size_t i=0;i<n;++i)
            a[i]=static_cast<float>(static_cast<int>(rng()%1000000)-500000)/779999.0f;
        if(n) {
            switch(trial%13) {
            case 0:a[n/2]=std::numeric_limits<float>::quiet_NaN();break;
            case 1:a[n/2]=std::numeric_limits<float>::infinity();break;
            case 2:a[n/2]=-std::numeric_limits<float>::infinity();break;
            case 3:a[n/2]=1e-7f;break;
            case 4:a[n/2]=0.0f;break;
            case 5:a[n/2]=std::numeric_limits<float>::max();break;
            default:break;
            }
        }
        const float* v=trial%17==0?nullptr:a.data();
        const auto reference=scalar(v,n);
        const auto faster=scan_pcm_channel(v,n);
        CHECK(reference.nonzero==faster.nonzero);
        CHECK(reference.invalid==faster.invalid);
        CHECK(reference.peak==faster.peak);
    }
#if MODERN_ASIO_PCM_HAS_SSE2
    // Avoid calling a signaling FP comparison on NaNs: DAW hosts may alter
    // the MXCSR exception mask. The SSE2 scanner must leave invalid clear.
    std::array<float,8> specials{
        1.0f,-1.0f,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),-0.0f,2.0f,1e-7f};
    const auto saved_mxcsr=_mm_getcsr();
    _mm_setcsr(saved_mxcsr & ~_MM_EXCEPT_INVALID);
    const auto result=scan_pcm_channel(specials.data(),specials.size());
    const bool invalid_raised=(_mm_getcsr() & _MM_EXCEPT_INVALID)!=0;
    _mm_setcsr(saved_mxcsr);
    CHECK(!invalid_raised);
    CHECK(result.invalid && result.nonzero && result.peak==2.0f);
#endif
    std::puts("PCM SIMD finite/NaN/Inf/peak scan differential PASS (50000 cases)");
}
