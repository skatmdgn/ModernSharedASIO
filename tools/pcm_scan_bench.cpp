#include "modern_asio/pcm_channel_scan.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
static volatile float sink=0.0f;
static modern_asio::ChannelScan scalar(const float* v,std::size_t frames) {
    modern_asio::ChannelScan result{};
    for(std::size_t i=0;i<frames;++i){
        const auto sample=v[i];
        if(!std::isfinite(sample))result.invalid=true;
        else {
            const auto mag=std::fabs(sample);
            if(mag>result.peak)result.peak=mag;
            if(mag>0.000001f)result.nonzero=true;
        }
    }
    return result;
}
int main(){
    std::array<float,512> values{};
    for(std::size_t i=0;i<512;++i)values[i]=static_cast<float>(static_cast<int>(i%23)-11)/113.0f;
    for(int i=0;i<8;++i) {
        const bool simd=(i&1)!=0;
        const auto begin=std::chrono::steady_clock::now();
        for(int k=0;k<1000000;++k) {
            const auto result=simd?modern_asio::scan_pcm_channel(values.data(),512)
                                   :scalar(values.data(),512);
            if((k%50000)==0)sink=sink+result.peak;
        }
        const auto nanos=std::chrono::duration<double,std::nano>(
            std::chrono::steady_clock::now()-begin).count()/1000000.0;
        std::printf("%s 512-sample channel PCM scan: %.2f ns\n",
                    simd?"SSE2":"scalar",nanos);
    }
}
