// Exact old-vs-optimized PCM equivalence, capacity/wrap/partial-write cases,
// and a simultaneous SPSC producer/consumer transfer of distinct frame IDs.
#include "modern_asio/asio_playback_bridge.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <thread>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" "<<#x<<"\n";std::abort();}}while(false)
namespace {
using modern_asio::SpscStereoRing;
using modern_asio::AsioPlaybackBridge;
std::size_t legacy_submit(SpscStereoRing& fifo,const float* l,const float* r,
                          std::size_t frames) {
    if((!l&&!r)||!frames)return 0;
    std::size_t sent=0;float tmp[256]{};
    while(sent<frames) {
        const auto n=std::min<std::size_t>(128,frames-sent);
        for(std::size_t i=0;i<n;++i){
            tmp[2*i]=l?l[sent+i]:0.0f;
            tmp[2*i+1]=r?r[sent+i]:0.0f;
        }
        const auto pushed=fifo.push(tmp,n);
        sent+=pushed;
        if(pushed!=n)break;
    }
    return sent;
}
void randomized_equivalence() {
    std::mt19937_64 rng(12345);
    for (std::size_t capacity:{2,4,8,16,256,512,1024,32768}) {
        SpscStereoRing reference(capacity),optimized_ring(capacity);
        AsioPlaybackBridge optimized(optimized_ring);
        std::array<float,2048> left{},right{};
        std::array<float,4096> out_old{},out_new{};
        for(int iter=0;iter<15000;++iter) {
            const auto count=static_cast<std::size_t>(rng()%1500);
            const int mode=static_cast<int>(rng()%4);
            for(std::size_t i=0;i<count;++i) {
                left[i]=static_cast<float>(rng()%1000000)/123456.0f;
                right[i]=-left[i];
            }
            const float* l=(mode==0||mode==1)?left.data():nullptr;
            const float* r=(mode==0||mode==2)?right.data():nullptr;
            const auto before=reference.available_frames();
            const auto old_n=legacy_submit(reference,l,r,count);
            const auto new_n=optimized.submit(l,r,count);
            CHECK(old_n==new_n);
            CHECK(reference.available_frames()==before+old_n);
            CHECK(optimized_ring.available_frames()==reference.available_frames());
            const auto read=static_cast<std::size_t>(rng()%1400);
            const auto a=reference.pop(out_old.data(),read);
            const auto b=optimized_ring.pop(out_new.data(),read);
            CHECK(a==b);
            for(std::size_t i=0;i<a*2;++i)CHECK(out_old[i]==out_new[i]);
        }
    }
}
void concurrent_transfer() {
    constexpr int kFrames=250000;
    SpscStereoRing ring(1024);
    std::thread producer([&]{
        int produced=0;
        std::array<float,512> left{},right{};
        while(produced<kFrames) {
            const int n=std::min(512,kFrames-produced);
            for(int i=0;i<n;++i){
                left[i]=static_cast<float>(produced+i);
                right[i]=-left[i];
            }
            const auto pushed=ring.push_planar(left.data(),right.data(),n);
            produced+=static_cast<int>(pushed);
            if(!pushed)std::this_thread::yield();
        }
    });
    std::array<float,1024> output{};
    int consumed=0;
    while(consumed<kFrames) {
        const auto got=ring.pop(output.data(),std::min(512,kFrames-consumed));
        for(std::size_t i=0;i<got;++i) {
            CHECK(output[2*i]==static_cast<float>(consumed+static_cast<int>(i)));
            CHECK(output[2*i+1]==-output[2*i]);
        }
        consumed+=static_cast<int>(got);
        if(!got)std::this_thread::yield();
    }
    producer.join();
    CHECK(ring.available_frames()==0);
}
}
int main(){
    randomized_equivalence();
    concurrent_transfer();
    std::cout<<"Optimized planar FIFO: PASS (120,000 randomized operations; 250,000 concurrent frames)\n";
}
