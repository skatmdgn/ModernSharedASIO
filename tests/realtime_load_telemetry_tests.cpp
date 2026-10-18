#include "modern_asio/realtime_load_telemetry.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<"\n";std::abort();}}while(false)
using namespace modern_asio;
int main(){
    CallbackTimingTracker host;
    constexpr std::uint64_t durations[]{0,250,500,501,1000,2000,4000,8000,12000,20000,20001};
    for(std::uint64_t i=0;i<11;++i)host.observe(durations[i],10666,i);
    auto h=host.snapshot_stopped();
    CHECK(h.samples==11 && h.buckets[0]==3 && h.buckets[1]==2);
    CHECK(h.buckets[2]==1 && h.buckets[3]==1 && h.buckets[4]==1);
    CHECK(h.buckets[5]==1 && h.buckets[6]==1 && h.buckets[7]==1);
    CHECK(h.over_budget==3 && h.longest_over_budget_streak==3);
    CHECK(h.first_over_budget_block==8);
    host.observe(12000,10666,11);
    h=host.snapshot_stopped();
    CHECK(h.longest_over_budget_streak==4);
    host.observe(100,10666,12);
    host.observe(13000,10666,13);
    h=host.snapshot_stopped();
    CHECK(h.over_budget==5 && h.longest_over_budget_streak==4);
    host.reset_stopped();
    CHECK(host.snapshot_stopped().samples==0);
    CHECK(host.snapshot_stopped().first_over_budget_block==UINT64_MAX);
    RenderQueueTracker render;
    CHECK(render.snapshot().samples==0 && render.snapshot().minimum_frames==UINT64_MAX);
    render.observe(1024,480,0);
    render.observe(300,480,1);
    render.observe(1504,480,2);
    render.observe(0,480,3);
    auto s=render.snapshot();
    CHECK(s.samples==4 && s.minimum_frames==0 && s.maximum_frames==1504);
    CHECK(s.below_demand_events==2 && s.first_below_demand_event==1);
    std::atomic<bool> done{false};
    std::thread reader([&]{while(!done.load(std::memory_order_acquire)){
        auto snapshot=render.snapshot();
        CHECK(snapshot.samples>=4 && snapshot.maximum_frames>=1504);
        CHECK(snapshot.first_below_demand_event==1);
    }});
    for(std::uint64_t i=0;i<100000;++i)render.observe(i%2048,480,4+i);
    done.store(true,std::memory_order_release);
    reader.join();
    s=render.snapshot();
    CHECK(s.samples==100004 && s.first_below_demand_event==1);
    render.reset_stopped();
    CHECK(render.snapshot().samples==0);
    CHECK(render.snapshot().minimum_frames==UINT64_MAX);
    std::cout<<"Realtime load telemetry tests PASS\n";
}
