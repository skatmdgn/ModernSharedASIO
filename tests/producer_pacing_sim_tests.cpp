#include "modern_asio/producer_fill_policy.hpp"
#include "modern_asio/spsc_stereo_ring.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" "<<#x<<"\n";std::abort();}}while(false)
static void simulate(std::size_t period) {
    modern_asio::SpscStereoRing fifo(4096);
    constexpr std::array<float,1024> block{};
    std::array<float,960> pcm{};
    std::uint64_t produced=0,consumed=0;
    CHECK(fifo.push(block.data(),512)==512);
    produced+=512; // ASIO B-half host pre-roll.
    for(int event=0;event<60000;++event) {
        const auto got=fifo.pop(pcm.data(),period);
        // Assumes immediate producer wake after each event; Windows jitter omitted.
        CHECK(got==period);
        consumed+=got;
        const auto count=modern_asio::producer_blocks_to_fill(
            fifo.available_frames(),fifo.capacity_frames(),512,1024,4);
        for(std::size_t b=0;b<count;++b){
            CHECK(fifo.push(block.data(),512)==512);
            produced+=512;
        }
        CHECK(produced-consumed==fifo.available_frames());
        CHECK(fifo.available_frames()<=1536);
    }
    std::cout<<period<<" frames/event: "<<consumed<<" consumed; "
             <<produced<<" produced; "<<fifo.available_frames()<<" queued\n";
}
int main(){simulate(441);simulate(480);}
