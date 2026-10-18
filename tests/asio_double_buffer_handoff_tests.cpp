#include "modern_asio/asio_double_buffer_handoff.hpp"
#include "modern_asio/asio_playback_bridge.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<" "<<#x<<"\n";std::abort();}}while(false)
int main() {
    using namespace modern_asio;
    CHECK(asio_output_half_for_callback(0)==1);
    CHECK(asio_output_half_for_callback(1)==0);
    CHECK(asio_output_half_for_callback(-1)==-1);
    CHECK(asio_output_half_for_callback(2)==-1);
    constexpr std::size_t frames=512;
    std::array<std::array<float,frames>,2> left{},right{};
    // The host may have prepared output B before the very first callback.
    left[1].fill(.25f);
    right[1].fill(-.25f);
    SpscStereoRing fifo(8192);
    AsioPlaybackBridge bridge(fifo);
    CHECK(fifo.available_frames()==0); // no duplicated initial B prefill
    for(int callback=0;callback<100;++callback) {
        const long write_half=callback&1;
        const long output_half=asio_output_half_for_callback(write_half);
        left[write_half].fill(static_cast<float>(callback+1));
        right[write_half].fill(-static_cast<float>(callback+1));
        // One callback fills its own half, but transfers the opposite half.
        CHECK(bridge.submit(left[output_half].data(),
                            right[output_half].data(),frames)==frames);
        std::array<float,frames*2> pcm{};
        CHECK(fifo.pop(pcm.data(),frames)==frames);
        const float expected=callback==0 ? .25f : static_cast<float>(callback);
        for(std::size_t frame=0;frame<frames;++frame) {
            CHECK(pcm[frame*2]==expected);
            CHECK(pcm[frame*2+1]==-expected);
        }
    }
    CHECK(fifo.available_frames()==0);
    CHECK(bridge.dropped_frames()==0);
    std::cout<<"ASIO alternate-half output handoff PASS (100 callbacks)\n";
}
