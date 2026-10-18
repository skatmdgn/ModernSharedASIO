#include "modern_asio/spsc_stereo_ring.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
int main(){
    using modern_asio::SpscStereoRing;
    for(std::size_t capacity:{2u,8u,512u,32768u}) {
        SpscStereoRing fifo(capacity);
        std::array<float,1024> old_pcm{},fresh_pcm{},read_pcm{};
        old_pcm.fill(0.75f);
        fresh_pcm.fill(-0.25f);
        for(int iteration=0;iteration<5000;++iteration) {
            fifo.clear_stopped();
            CHECK(fifo.available_frames()==0);
            CHECK(fifo.pop(read_pcm.data(),1)==0);
            const auto count=std::min<std::size_t>(capacity,512);
            CHECK(fifo.push(old_pcm.data(),count)==count);
            // Old unconsumed audio remains in storage, but after reset its
            // indexes MUST make it completely inaccessible to the consumer.
            fifo.clear_stopped();
            CHECK(fifo.pop(read_pcm.data(),count)==0);
            CHECK(fifo.push(fresh_pcm.data(),count)==count);
            CHECK(fifo.pop(read_pcm.data(),count)==count);
            for(std::size_t j=0;j<count*2;++j)CHECK(read_pcm[j]==-0.25f);
        }
    }
    std::puts("SPSC reset index-only no-stale-PCM PASS");
}
