#include "modern_asio/asio_callback_timeline.hpp"
#include <cstdint>
#include <iostream>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::cerr<<"FAILED line "<<__LINE__<<": "<<#x<<"\\n";std::abort();} } while(false)
int main(){
    modern_asio::AsioCallbackTimeline t;
    CHECK(t.current()==0);
    t.reset();
    // Simulate submission of prefilled ASIO output B: must NOT advance
    // ASIO callback position.
    constexpr std::uint32_t prefilled_output_frames=512;
    (void)prefilled_output_frames;
    CHECK(t.current()==0);
    for(std::uint64_t callback=0;callback<100000;++callback){
        CHECK(t.current()==callback*512ULL);
        t.complete_callback(512);
    }
    CHECK(t.current()==100000ULL*512ULL);
    t.reset();
    CHECK(t.current()==0);
    t.complete_callback(256);
    CHECK(t.current()==256);
    std::cout<<"ASIO callback timeline: PASS\\n";
}
