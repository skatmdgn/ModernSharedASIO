// Optional non-CTest microbenchmark; measure bridge-only cost, not Cubase latency.
#include "modern_asio/asio_playback_bridge.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
using modern_asio::SpscStereoRing;
using modern_asio::AsioPlaybackBridge;
static std::size_t legacy_submit(SpscStereoRing& q,const float* l,const float* r,std::size_t frames) {
    if((!l&&!r)||!frames)return 0;
    std::size_t sent=0;float tmp[256];
    while(sent<frames){
        const auto n=std::min<std::size_t>(128,frames-sent);
        for(std::size_t i=0;i<n;++i) {
            tmp[i*2]=l?l[sent+i]:0.0f;
            tmp[i*2+1]=r?r[sent+i]:0.0f;
        }
        const auto pushed=q.push(tmp,n);
        sent+=pushed;
        if(pushed!=n)break;
    }
    return sent;
}
static volatile float output_sink=0.0f;
static double measure(bool fast,bool mono,int blocks) {
    SpscStereoRing fifo(32768);
    AsioPlaybackBridge bridge(fifo);
    std::array<float,512> l{},r{};
    std::array<float,1024> out{};
    for(int i=0;i<512;++i){l[i]=i*.001f;r[i]=1.0f-l[i];}
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<blocks;++i) {
        const auto count=fast?bridge.submit(l.data(),mono?nullptr:r.data(),512)
                         :legacy_submit(fifo,l.data(),mono?nullptr:r.data(),512);
        if(count!=512||fifo.pop(out.data(),512)!=512)std::abort();
        if((i&8191)==0) output_sink=output_sink+out[0];
    }
    return std::chrono::duration<double,std::micro>(
        std::chrono::steady_clock::now()-start).count()/blocks;
}
int main() {
    for(bool mono:{false,true}){
        double old_total=0,new_total=0;
        // Alternating order reduces thermal/CPU drift bias.
        for(int run=0;run<6;++run) {
            const auto elapsed=measure((run&1)!=0,240000,mono);
            if(run&1)new_total+=elapsed;else old_total+=elapsed;
        }
        std::printf("%s: legacy %.4f us/block; optimized %.4f us/block; speedup %.2fx\n",
                    mono?"mono":"stereo",old_total/3,new_total/3,old_total/new_total);
    }
}
