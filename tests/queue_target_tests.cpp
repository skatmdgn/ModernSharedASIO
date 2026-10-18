// Deterministic steady-state FIFO occupancy with both safe default and
// experimental low-latency target. Deliberately NOT a Windows jitter simulation.
#include "modern_asio/producer_fill_policy.hpp"
#include "modern_asio/spsc_stereo_ring.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
static double simulate(std::size_t period,std::size_t target) {
    modern_asio::SpscStereoRing fifo(32768);
    constexpr std::array<float,1024> silent{};
    std::array<float,960> scratch{};
    std::uint64_t produced=0,consumed=0,queued_sum=0;
    for(int event=0;event<200000;++event) {
        if(event>0){
            CHECK(fifo.pop(scratch.data(),period)==period);
            consumed+=period;
        }
        const auto count=modern_asio::producer_blocks_to_fill(
            fifo.available_frames(),fifo.capacity_frames(),512,target,4);
        for(std::size_t i=0;i<count;++i) {
            CHECK(fifo.push(silent.data(),512)==512);
            produced+=512;
        }
        CHECK(produced-consumed==fifo.available_frames());
        CHECK(fifo.available_frames()>=target);
        CHECK(fifo.available_frames()<=target+511);
        queued_sum+=fifo.available_frames();
    }
    const double average=static_cast<double>(queued_sum)/200000.0;
    std::printf("period=%zu target=%zu mean queued=%.2f frames, %.3f ms @48k\n",
                period,target,average,average/48.0);
    return average;
}
int main(){
    for(std::size_t period:{441,480}){
        const auto low=simulate(period,512);
        const auto baseline=simulate(period,1024);
        CHECK(baseline-low>500.0 && baseline-low<525.0);
    }
    std::puts("Queue target timing model PASS; does not measure Windows dropouts");
}
