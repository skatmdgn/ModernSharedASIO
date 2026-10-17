#include "modern_asio/engine_demand.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if(!(x)) std::abort(); } while(false)
using modern_asio::SpscStereoRing;
struct Source {
    SpscStereoRing* fifo{};
    int calls{0};
    bool fail{false};
    bool make_no_progress{false};
    static bool request(void* raw) noexcept {
        auto& s=*static_cast<Source*>(raw);
        ++s.calls;
        if(s.fail)return false;
        if(s.make_no_progress)return true;
        constexpr std::array<float,8> block{1,2,3,4,5,6,7,8};
        return s.fifo->push(block.data(),4)==4;
    }
};
int main() {
    SpscStereoRing fifo(16);
    Source producer{&fifo};
    const auto a=modern_asio::fulfill_audio_demand(fifo,9,4,Source::request,&producer);
    CHECK(a.requests==3 && a.available_frames==12 && !a.stalled);
    const auto b=modern_asio::fulfill_audio_demand(fifo,9,4,Source::request,&producer);
    CHECK(b.requests==0 && producer.calls==3);
    std::array<float,32> data{};
    CHECK(fifo.pop(data.data(),12)==12);
    producer.fail=true;
    const auto c=modern_asio::fulfill_audio_demand(fifo,8,4,Source::request,&producer);
    CHECK(c.requests==1 && c.stalled && c.available_frames==0);
    producer.fail=false;
    producer.make_no_progress=true;
    const auto stalled=modern_asio::fulfill_audio_demand(fifo,8,4,Source::request,&producer);
    CHECK(stalled.requests==1 && stalled.stalled && stalled.available_frames==0);
    producer.make_no_progress=false;
    const auto d=modern_asio::fulfill_audio_demand(fifo,20,4,Source::request,&producer);
    CHECK(d.requests==4 && d.available_frames==16 && !d.stalled);
    CHECK(fifo.pop(data.data(),16)==16);
    const auto e=modern_asio::fulfill_audio_demand(fifo,3,0,Source::request,&producer);
    CHECK(e.requests==0 && e.available_frames==0);
    const auto f=modern_asio::fulfill_audio_demand(fifo,3,64,Source::request,&producer);
    CHECK(f.requests==0 && f.available_frames==0);
    std::cout<<"engine-demand tests: PASS\n";
}
