#include "modern_asio/asio_block_stamp.hpp"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<" "<<#x<<"\n";std::abort();}}while(false)
int main() {
    modern_asio::AsioBlockStampChannel stamps;
    modern_asio::AsioBlockStamp value{};
    CHECK(stamps.read(value) && value.frame==0 && value.system_time_ns==0);
    constexpr std::uint64_t epoch=2000000000ULL;
    stamps.publish({0,epoch});
    CHECK(stamps.read(value) && value.frame==0 && value.system_time_ns==epoch);
    // A whole frame/time pair is published before each host bufferSwitch.
    stamps.publish({512,epoch+10666666});
    CHECK(stamps.read(value) && value.frame==512 && value.system_time_ns==epoch+10666666);
    std::atomic<bool> finished{false};
    std::thread producer([&]{
        for(std::uint64_t i=1;i<=250000;++i)
            stamps.publish({i*512ULL,epoch+i*10666666ULL});
        finished.store(true,std::memory_order_release);
    });
    unsigned coherent_reads=0;
    while(!finished.load(std::memory_order_acquire)) {
        if(stamps.read(value)) {
            CHECK(value.frame%512==0);
            CHECK(value.system_time_ns==epoch+(value.frame/512ULL)*10666666ULL);
            ++coherent_reads;
        }
    }
    producer.join();
    CHECK(stamps.read(value));
    CHECK(value.frame==250000ULL*512ULL);
    CHECK(value.system_time_ns==epoch+250000ULL*10666666ULL);
    std::cout<<"ASIO block-stamp coherence: PASS, reads="<<coherent_reads<<"\n";
}
