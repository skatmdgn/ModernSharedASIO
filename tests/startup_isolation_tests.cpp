#include "modern_asio/engine_demand.hpp"
#include <array>
#include <cstdlib>
#include <cstdio>
#define CHECK(x) do {if(!(x))std::abort();}while(false)
struct Source {
 modern_asio::SpscStereoRing* fifo;
 int calls{0};
 static bool run(void* ctx) noexcept {
   auto& s=*static_cast<Source*>(ctx);
   ++s.calls;
   const std::array<float,8> data{1,2,3,4,5,6,7,8};
   return s.fifo->push(data.data(),4)==4;
 }
};
int main(){
 modern_asio::SpscStereoRing fifo(16);
 Source s{&fifo};
 auto prime=modern_asio::fulfill_audio_demand(fifo,8,4,Source::run,&s,false);
 CHECK(prime.requests==0 && prime.available_frames==0 && s.calls==0);
 auto live=modern_asio::fulfill_audio_demand(fifo,8,4,Source::run,&s,true);
 CHECK(live.requests==2 && live.available_frames==8 && s.calls==2);
 auto prime_again=modern_asio::fulfill_audio_demand(fifo,16,4,Source::run,&s,false);
 CHECK(prime_again.requests==0 && prime_again.available_frames==8 && s.calls==2);
 auto more=modern_asio::fulfill_audio_demand(fifo,16,4,Source::run,&s,true);
 CHECK(more.requests==2 && more.available_frames==16);
 std::printf("startup callback isolation PASS\n");
}
