#include "modern_asio/producer_fill_policy.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n"; std::abort();} } while(false)
int main(){
 using modern_asio::producer_blocks_to_fill;
 CHECK(producer_blocks_to_fill(0,4096,512,1024,4)==2);
 CHECK(producer_blocks_to_fill(512,4096,512,1024,4)==1);
 CHECK(producer_blocks_to_fill(1024,4096,512,1024,4)==0);
 CHECK(producer_blocks_to_fill(0,4096,512,4096,3)==3);
 CHECK(producer_blocks_to_fill(4010,4096,512,4096,4)==0);
 CHECK(producer_blocks_to_fill(0,4096,0,4096,4)==0);
 CHECK(producer_blocks_to_fill(0,4096,8192,4096,4)==0);
 CHECK(producer_blocks_to_fill(0,4096,512,4096,0)==0);
 CHECK(producer_blocks_to_fill(4100,4096,512,4096,4)==0);
 CHECK(producer_blocks_to_fill(0,4096,512,6000,20)==8);
 CHECK(producer_blocks_to_fill(0,std::numeric_limits<std::size_t>::max(),512,
       std::numeric_limits<std::size_t>::max(),4)==4);
 std::mt19937_64 rng(0x65B2);
 for(int i=0;i<60000;++i){
   const std::size_t cap=2+rng()%8190,used=rng()%(cap+1),block=1+rng()%1024;
   const std::size_t target=rng()%12288,maxn=rng()%12;
   const auto n=producer_blocks_to_fill(used,cap,block,target,maxn);
   CHECK(n<=maxn && n<=(cap-used)/block);
   std::size_t expected=0;
   const auto desired=std::min(target,cap);
   while(expected<maxn && used+expected*block<desired &&
         used+(expected+1)*block<=cap)++expected;
   CHECK(n==expected);
 }
 std::cout<<"producer policy PASS (60,000 randomized cases)\n";
}
