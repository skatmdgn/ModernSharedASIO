#include "modern_asio/clock_observation.hpp"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <thread>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" "<<#x<<"\n";std::abort();}}while(false)
int main(){
    using namespace modern_asio;
    std::uint64_t value=0;
    CHECK(!device_ticks_to_frames({123,0,4},48000,value));
    CHECK(!device_ticks_to_frames({123,48000,4},0,value));
    CHECK(device_ticks_to_frames({48000,48000,10000000},48000,value) && value==48000);
    CHECK(device_ticks_to_frames({441000,10000000,1000000},44100,value) && value==1944);
    CHECK(device_ticks_to_frames({12345,1000,0},48000,value) && value==592560);
    CHECK(!device_ticks_to_frames({std::numeric_limits<std::uint64_t>::max(),1,0},48000,value));
    CHECK(!device_ticks_to_frames({std::numeric_limits<std::uint64_t>::max()-1,
                                   std::numeric_limits<std::uint64_t>::max(),0},48000,value));
    CHECK(qpc_to_asio_nanoseconds(10000000,value) && value==1000000000ULL);
    CHECK(!qpc_to_asio_nanoseconds(std::numeric_limits<std::uint64_t>::max(),value));
#if defined(__SIZEOF_INT128__)
    // Portable MSVC implementation cross-checked against exact 128-bit
    // arithmetic on Clang/GCC; Windows/MSVC retains the ordinary test set.
    std::mt19937_64 rng(0x43AB671);
    for(int i=0;i<100000;++i){
        const std::uint64_t ticks=rng()%1000000000000000ULL;
        const std::uint64_t freq=1+rng()%1000000000ULL;
        const std::uint32_t rate=1+static_cast<std::uint32_t>(rng()%384000ULL);
        const auto exact=(static_cast<unsigned __int128>(ticks)*rate)/freq;
        const bool valid=exact<=std::numeric_limits<std::uint64_t>::max();
        const bool computed=device_ticks_to_frames({ticks,freq,0},rate,value);
        CHECK(valid==computed);
        if(computed)CHECK(value==static_cast<std::uint64_t>(exact));
    }
#endif
    ClockObservationChannel channel;
    ClockObservation sample{};
    CHECK(!channel.read(sample));
    channel.publish({8000,9000,10000});
    CHECK(channel.read(sample) && sample.device_ticks==8000 &&
          sample.device_frequency==9000 && sample.qpc_100ns==10000);
    std::atomic<bool> finished{false};
    std::thread writer([&]{
        for(std::uint64_t i=1;i<=400000;++i)channel.publish({i,i+1000,i+2000});
        finished.store(true,std::memory_order_release);
    });
    std::uint64_t coherent=0;
    while(!finished.load(std::memory_order_acquire)){
        if(channel.read(sample)){
            CHECK(sample.device_frequency==sample.device_ticks+1000);
            CHECK(sample.qpc_100ns==sample.device_ticks+2000);
            ++coherent;
        }
    }
    writer.join();
    CHECK(channel.read(sample) && sample.device_ticks==400000);
    channel.reset_stopped();
    CHECK(!channel.read(sample));
    std::cout<<"clock observation PASS; coherent reads="<<coherent<<"\n";
}
