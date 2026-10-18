// Manual only: microbenchmark WASAPI native-float transfer without actual device.
// An optimized PCM-memory-path speedup does not prove lower end-to-end audio latency.
#include "modern_asio/float32_output_transfer.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using modern_asio::SpscStereoRing;
static volatile float guard=0;
static double run(bool direct,int count) {
    SpscStereoRing fifo(32768);
    std::array<float,2112> samples{},scratch{},dest{};
    for(std::size_t i=0;i<samples.size();++i)samples[i]=static_cast<float>(i%17)/13.0f;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i){
        if(fifo.push(samples.data(),1056)!=1056)std::abort();
        if(direct){
            const auto r=modern_asio::fill_native_stereo_float32(fifo,dest.data(),1056);
            if(r.copied_frames!=1056)std::abort();
        }else{
            if(fifo.pop(scratch.data(),1056)!=1056)std::abort();
            std::memcpy(dest.data(),scratch.data(),1056*2*sizeof(float));
        }
        if(!(i&8191))guard=guard+dest[0];
    }
    const auto elapsed=std::chrono::duration<double,std::micro>(
        std::chrono::steady_clock::now()-start).count();
    return elapsed/count;
}
int main(){
    constexpr int count=200000;
    double oldsum=0,newsum=0;
    for(int i=0;i<8;++i){
        if(i%2) newsum+=run(true,count);
        else oldsum+=run(false,count);
    }
    const double before=oldsum/4,after=newsum/4;
    std::printf("1056-frame float32 WASAPI buffer: staged %.4f us/call, direct %.4f us/call, speedup %.3fx\n",
                before,after,before/after);
}
