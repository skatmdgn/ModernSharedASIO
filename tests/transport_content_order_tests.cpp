// Long musical timeline content-integrity test for the SPSC stereo bridge.
// Models sixteen 4/4 bars of complete silence at 120bpm, then a marker.
// Tests that a "bar 17" tone cannot be output while the transport is at bar 1
// due solely to FIFO reordering or bounded producer lookahead.
#include "modern_asio/asio_playback_bridge.hpp"
#include "modern_asio/producer_fill_policy.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
namespace {
constexpr std::uint64_t kRate=48000, kBarFrames=4*kRate/2;
constexpr std::uint64_t kMarker=16*kBarFrames; // first frame of bar 17
constexpr std::uint64_t kInitialPreroll=512;
constexpr std::size_t kBlock=512, kPeriod=480;
float sample_at(std::uint64_t pos) {
    if(pos<kMarker) return 0.0f;
    // Distinctive block-varying marker content with easily asserted identity.
    return (static_cast<float>((pos-kMarker)%997)/996.0f)*0.5f+0.25f;
}
void run() {
    modern_asio::SpscStereoRing fifo(32768);
    modern_asio::AsioPlaybackBridge bridge(fifo);
    std::array<float,kBlock> left{},right{};
    std::array<float,kPeriod*2> output{};
    CHECK(bridge.submit(left.data(),right.data(),kBlock)==kBlock); // B preroll.
    std::uint64_t produced=0,consumed=0;
    std::uint64_t first_marker=UINT64_MAX;
    const std::uint64_t target=kInitialPreroll+kMarker+2*kBarFrames;
    while(consumed<target){
        const auto batch=static_cast<std::size_t>(std::min<std::uint64_t>(kPeriod,target-consumed));
        const auto got=fifo.pop(output.data(),batch);
        CHECK(got==batch); // no missing frames accepted in this deterministic test
        for(std::size_t i=0;i<got;++i) {
            const auto output_index=consumed+i;
            const auto source_index=output_index-kInitialPreroll;
            const float expected=output_index<kInitialPreroll?0.0f:sample_at(source_index);
            CHECK(output[i*2]==expected);
            CHECK(output[i*2+1]==-expected || (output[i*2+1]==0.0f && expected==0.0f));
            if(expected!=0.0f && first_marker==UINT64_MAX) first_marker=output_index;
        }
        consumed+=batch;
        const auto planned=modern_asio::producer_blocks_to_fill(fifo.available_frames(),
                             fifo.capacity_frames(),kBlock,2*kBlock,4);
        for(std::size_t b=0;b<planned;++b){
            for(std::size_t j=0;j<kBlock;++j) {
                left[j]=sample_at(produced+j);
                right[j]=-left[j];
            }
            CHECK(bridge.submit(left.data(),right.data(),kBlock)==kBlock);
            produced+=kBlock;
        }
        CHECK(fifo.available_frames()<=2*kBlock+kBlock);
    }
    CHECK(first_marker==kInitialPreroll+kMarker);
    CHECK(bridge.dropped_frames()==0);
    std::printf("16-bar silence+bar17 marker integrity PASS; first marker at output frame %llu\n",
           static_cast<unsigned long long>(first_marker));
}
}
int main(){run();}
