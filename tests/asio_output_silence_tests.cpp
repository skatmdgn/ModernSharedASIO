#include "modern_asio/asio_output_silence.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<" "<<#x<<"\n";std::abort();}}while(false)
int main() {
    std::array<std::array<float,512>,2> a{},b{};
    for(auto& v:a) v.fill(0.75f);
    for(auto& v:b) v.fill(-0.4f);
    // Reusing half 0 after a transport seek must not retain its old PCM.
    modern_asio::clear_asio_output_planes(a[0].data(),b[0].data(),512);
    for(float v:a[0])CHECK(v==0.0f);
    for(float v:b[0])CHECK(v==0.0f);
    for(float v:a[1])CHECK(v==0.75f); // only requested half cleared
    for(float v:b[1])CHECK(v==-0.4f);
    a[0][511]=1.0f; b[0][511]=-1.0f;
    // If the next callback writes no data, it remains silence not stale tone.
    modern_asio::clear_asio_output_planes(a[0].data(),b[0].data(),512);
    CHECK(a[0][511]==0.0f && b[0][511]==0.0f);
    modern_asio::clear_asio_output_planes(nullptr,b[1].data(),512);
    for(float v:a[1])CHECK(v==0.75f);
    for(float v:b[1])CHECK(v==0.0f);
    std::cout<<"ASIO reused-plane clearing PASS\n";
}
