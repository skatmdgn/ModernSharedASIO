#include "modern_asio/producer_wake_policy.hpp"
#include "modern_asio/producer_fill_policy.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);std::abort();} } while(false)
#include <cstddef>
#include <cstdint>
#include <cstdio>
struct Stats {
    std::uint64_t wakes=0,callbacks=0,underruns=0;
    std::size_t queued=0;
};
static Stats simulate(std::size_t period,std::size_t target,bool conditional) {
    constexpr std::size_t block=512,capacity=32768;
    Stats s{};
    s.queued=target;
    for(int event=0;event<100000;++event){
        if(s.queued<period){
            s.underruns+=period-s.queued;
            s.queued=0;
        }else s.queued-=period;
        if(!conditional || modern_asio::producer_needs_wakeup(s.queued,target)) {
            ++s.wakes;
            const auto blocks=modern_asio::producer_blocks_to_fill(
                s.queued,capacity,block,target,4);
            s.callbacks+=blocks;
            s.queued+=blocks*block;
        }
    }
    return s;
}
int main(){
    using modern_asio::producer_needs_wakeup;
    CHECK(!producer_needs_wakeup(1024,1024));
    CHECK(producer_needs_wakeup(1023,1024));
    CHECK(producer_needs_wakeup(0,1024));
    CHECK(producer_needs_wakeup(1024,0));
    for(auto p:{441u,480u,512u,960u,1056u}){
        for(auto target:{512u,1024u}){
            const auto old=simulate(p,target,false);
            const auto optimized=simulate(p,target,true);
            CHECK(old.underruns==optimized.underruns);
            CHECK(old.callbacks==optimized.callbacks);
            CHECK(old.queued==optimized.queued);
            CHECK(optimized.wakes<=old.wakes);
            std::printf("period=%u target=%u old_wakes=%llu new_wakes=%llu underruns=%llu\n",
                p,target,static_cast<unsigned long long>(old.wakes),
                static_cast<unsigned long long>(optimized.wakes),
                static_cast<unsigned long long>(optimized.underruns));
        }
    }
    std::puts("Producer event wake predicate equivalence PASS");
}
