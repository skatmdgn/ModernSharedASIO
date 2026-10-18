#include "modern_asio/scheduler_gap_tracker.hpp"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <thread>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
int main() {
    modern_asio::SchedulerGapTracker tracker;
    auto s=tracker.snapshot();
    CHECK(s.samples==0 && s.first_over_threshold_ordinal==UINT64_MAX);
    tracker.observe_ticks(0,100,10000000,100,1);
    tracker.observe_ticks(200,199,10000000,100,2);
    tracker.observe_ticks(200,300,0,100,3);
    CHECK(tracker.snapshot().samples==0);
    // 4800 ticks @10MHz = 480us; first threshold is strict >500us.
    tracker.observe_ticks(100000,104800,10000000,500,4);
    tracker.observe_ticks(100000,106000,10000000,500,5);
    tracker.observe_ticks(100000,125000,10000000,500,6);
    s=tracker.snapshot();
    CHECK(s.samples==3 && s.total_us==480+600+2500);
    CHECK(s.maximum_us==2500 && s.over_threshold==2);
    CHECK(s.first_over_threshold_ordinal==5);
    // Both tracker readers and one producer are allowed concurrently.
    std::atomic<bool> done{false};
    std::thread reader([&] {
        while(!done.load(std::memory_order_acquire)) {
            const auto x=tracker.snapshot();
            CHECK(x.samples>=3 && x.maximum_us>=2500);
            CHECK(x.first_over_threshold_ordinal==5);
        }
    });
    for(std::uint64_t i=0;i<100000;++i)
        tracker.observe_microseconds(i%1000+1,500,i+7);
    done.store(true,std::memory_order_release);
    reader.join();
    s=tracker.snapshot();
    CHECK(s.samples==100003 && s.first_over_threshold_ordinal==5);
    tracker.reset_stopped();
    s=tracker.snapshot();
    CHECK(s.samples==0 && s.total_us==0 && s.maximum_us==0);
    CHECK(s.first_over_threshold_ordinal==UINT64_MAX);
    std::puts("Scheduler gap opt-in tracker PASS");
}
