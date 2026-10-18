// The host-visible 256- and 512-frame ASIO modes must obey exactly the same
// half-buffer handoff, sample-timeline, and producer policy invariants.
#include "modern_asio/asio_playback_bridge.hpp"
#include "modern_asio/asio_callback_timeline.hpp"
#include "modern_asio/asio_double_buffer_handoff.hpp"
#include "modern_asio/producer_fill_policy.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();} } while(false)
static void callback_modes(std::size_t frames) {
    modern_asio::SpscStereoRing ring(16384);
    modern_asio::AsioPlaybackBridge bridge(ring);
    modern_asio::AsioCallbackTimeline timeline;
    std::array<std::array<float,512>,2> left{},right{};
    std::array<float,1024> output{};
    left[1].fill(.25f);right[1].fill(-.25f);
    CHECK(timeline.current()==0);
    for(int call=0;call<5000;++call) {
        const int write=call&1,output_half=modern_asio::asio_output_half_for_callback(write);
        CHECK(output_half==(write^1));
        CHECK(timeline.current()==static_cast<std::uint64_t>(call)*frames);
        std::fill_n(left[write].data(),frames,static_cast<float>(call+1));
        std::fill_n(right[write].data(),frames,-static_cast<float>(call+1));
        CHECK(bridge.submit(left[output_half].data(),right[output_half].data(),frames)==frames);
        CHECK(ring.pop(output.data(),frames)==frames);
        const float expected=call==0?.25f:static_cast<float>(call);
        for(std::size_t i=0;i<frames;++i) {
            CHECK(output[2*i]==expected && output[2*i+1]==-expected);
        }
        timeline.complete_callback(static_cast<std::uint32_t>(frames));
    }
    CHECK(timeline.current()==5000*frames);
    CHECK(ring.available_frames()==0 && bridge.dropped_frames()==0);
}
static void producer_modes(std::size_t frames,std::size_t device_period) {
    modern_asio::SpscStereoRing fifo(32768);
    std::array<float,1024> stereo{};
    std::array<float,2048> output{};
    std::uint64_t produced=0,consumed=0;
    // Startup prime of WASAPI is silence independent of ASIO producer.
    for(int i=0;i<200000;++i) {
        const auto count=modern_asio::producer_blocks_to_fill(
            fifo.available_frames(),fifo.capacity_frames(),frames,2*frames,4);
        for(std::size_t b=0;b<count;++b){
            CHECK(fifo.push(stereo.data(),frames)==frames);
            produced+=frames;
        }
        // Model the WASAPI render event consuming its entire period.
        CHECK(fifo.pop(output.data(),device_period)==device_period);
        consumed+=device_period;
        CHECK(produced-consumed==fifo.available_frames());
        CHECK(fifo.available_frames()<=3*frames);
    }
}
int main() {
    for(std::size_t frames:{256u,512u}){
        callback_modes(frames);
        producer_modes(frames,480);
        producer_modes(frames,441);
    }
    std::puts("ASIO 256/512 buffer contract + demand modes: PASS");
}
