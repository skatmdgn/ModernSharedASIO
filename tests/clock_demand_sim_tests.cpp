#include "modern_asio/engine_demand.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) std::abort(); } while(false)
struct Producer {
    modern_asio::SpscStereoRing* fifo;
    std::uint64_t blocks{0};
    static bool generate(void* raw) noexcept {
        auto& s=*static_cast<Producer*>(raw);
        constexpr std::array<float,1024> block{};
        if(s.fifo->push(block.data(),512)!=512)return false;
        ++s.blocks;return true;
    }
};
static void test(std::uint32_t period_frames) {
    modern_asio::SpscStereoRing fifo(4096);
    Producer producer{&fifo};
    std::array<float,960> buffer{};
    std::uint64_t consumed=0;
    for(int event=0;event<60000;++event) {
        auto d=modern_asio::fulfill_audio_demand(
            fifo,period_frames,512,Producer::generate,&producer);
        CHECK(!d.stalled);
        CHECK(fifo.pop(buffer.data(),period_frames)==period_frames);
        consumed+=period_frames;
        const auto generated=producer.blocks*512;
        CHECK(generated>=consumed && generated-consumed<512);
        CHECK(fifo.available_frames()==generated-consumed);
    }
    CHECK(producer.blocks==(consumed+511)/512);
    std::printf("frames per event: %u; produced blocks: %llu\n",
                period_frames,static_cast<unsigned long long>(producer.blocks));
}
int main(){test(441);test(480);}
