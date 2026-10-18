#include "modern_asio/underrun_event_tracker.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAILED line "<<__LINE__<<" "<<#x<<"\n";std::abort();}}while(false)
int main() {
    modern_asio::UnderrunEventTracker t;
    CHECK(t.snapshot().events==0);
    CHECK(t.snapshot().first_event==UINT64_MAX);
    t.record(0,1);CHECK(t.snapshot().events==0);
    t.record(480,7);t.record(16,9);t.record(1056,20);
    CHECK(t.snapshot().events==3);
    CHECK(t.snapshot().max_missing_frames==1056);
    CHECK(t.snapshot().first_event==7);
    std::atomic<bool> finished{false};
    std::thread reader([&] {
        while(!finished.load(std::memory_order_acquire)){
            const auto s=t.snapshot();
            CHECK(s.events>=3 && s.max_missing_frames>=1056);
        }
    });
    for(std::uint64_t i=0;i<100000;++i)t.record(i%200+1,i+21);
    finished.store(true,std::memory_order_release);
    reader.join();
    CHECK(t.snapshot().events==100003);
    CHECK(t.snapshot().first_event==7);
    CHECK(t.snapshot().max_missing_frames==1056);
    t.reset_stopped();
    CHECK(t.snapshot().events==0 && t.snapshot().first_event==UINT64_MAX);
    std::cout<<"Underrun event instrumentation PASS\n";
}
