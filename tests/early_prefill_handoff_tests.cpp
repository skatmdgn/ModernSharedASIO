#include "modern_asio/asio_playback_bridge.hpp"
#include "modern_asio/asio_double_buffer_handoff.hpp"
#include "modern_asio/asio_callback_timeline.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(false)
int main() {
    using namespace modern_asio;
    SpscStereoRing fifo(2048);
    AsioPlaybackBridge bridge(fifo);
    AsioCallbackTimeline timeline;
    std::array<std::array<float,512>,2> left{},right{};
    left[1].fill(.25f);right[1].fill(-.25f);
    for(int index=0;index<2;++index) {
        const auto output=asio_output_half_for_callback(index);
        CHECK(output==(index^1));
        left[index].fill(float(index+1));right[index].fill(-float(index+1));
        CHECK(timeline.current()==static_cast<unsigned long long>(index)*512);
        CHECK(bridge.submit(left[output].data(),right[output].data(),512)==512);
        timeline.complete_callback(512);
    }
    CHECK(fifo.available_frames()==1024);
    std::array<float,960> frames{};
    CHECK(fifo.pop(frames.data(),480)==480);
    for(int i=0;i<480;++i)CHECK(frames[i*2]==.25f&&frames[i*2+1]==-.25f);
    CHECK(fifo.pop(frames.data(),480)==480);
    for(int i=0;i<480;++i) {
        const float expected=i<32?.25f:1.0f;
        CHECK(frames[i*2]==expected&&frames[i*2+1]==-expected);
    }
    CHECK(timeline.current()==1024 && fifo.available_frames()==64);
    CHECK(bridge.dropped_frames()==0);
    std::puts("early producer prefill preserves ASIO opposite-half PCM order: PASS");
}
