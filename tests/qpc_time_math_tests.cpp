#include "modern_asio/qpc_time_math.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);std::abort();} } while(false)
#include <cstdint>
#include <iostream>
int main() {
    using modern_asio::qpc_ticks_to_ns;
    using modern_asio::qpc_elapsed_us;
    CHECK(qpc_ticks_to_ns(10000000ULL,10000000ULL)==1000000000ULL);
    CHECK(qpc_ticks_to_ns(123456789ULL,10000000ULL)==12345678900ULL);
    CHECK(qpc_ticks_to_ns(7,0)==0);
    CHECK(qpc_elapsed_us(111,101,10000000)==0);
    CHECK(qpc_elapsed_us(111,120,0)==0);
    for (std::uint64_t frequency: {10000000ULL, 3579545ULL, 3000000ULL, 1000000000ULL}) {
        for (std::uint64_t ticks=0;ticks<2000000;ticks+=1237) {
            const std::uint64_t begin=315000000000ULL;
            const auto old_start=qpc_ticks_to_ns(begin,frequency);
            const auto old_end=qpc_ticks_to_ns(begin+ticks,frequency);
            const auto old_delta_us=(old_end-old_start)/1000;
            const auto fresh=qpc_elapsed_us(begin,begin+ticks,frequency);
            // Absolute ns truncation of the old implementation can differ
            // by one microsecond from exact elapsed tick conversion.
            CHECK(fresh<=old_delta_us+1 && old_delta_us<=fresh+1);
        }
    }
    std::cout<<"QPC cached-frequency tick math PASS\n";
}
