#include "modern_asio/asio_playback_bridge.hpp"
#include <array>
#include <cassert>
#include <iostream>
using modern_asio::SpscStereoRing;
using modern_asio::AsioPlaybackBridge;
int main() {
    SpscStereoRing ring(8);
    AsioPlaybackBridge bridge(ring);
    const std::array<float, 20> left{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    const std::array<float, 20> right{-1,-2,-3,-4,-5,-6,-7,-8,-9,-10,-11,-12,-13,-14,-15,-16,-17,-18,-19,-20};
    std::array<float,16> out{};
    assert(bridge.submit(left.data(),right.data(),6)==6);
    assert(ring.pop(out.data(),4)==4);
    for(int i=0;i<4;i++){assert(out[2*i]==left[i]);assert(out[2*i+1]==right[i]);}
    assert(bridge.submit(left.data()+6,right.data()+6,10)==6);
    assert(bridge.dropped_frames()==4);
    assert(ring.pop(out.data(),8)==8);
    for(int i=0;i<8;i++){assert(out[i*2]==left[i+4]);assert(out[i*2+1]==right[i+4]);}
    assert(bridge.submit(nullptr,right.data(),4)==4);
    assert(ring.pop(out.data(),4)==4);
    for(int i=0;i<4;i++){assert(out[2*i]==0.0f);assert(out[2*i+1]==right[i]);}
    ring.clear_stopped();
    assert(ring.available_frames()==0);
    assert(bridge.submit(nullptr,nullptr,4)==0);
    bool threw=false; try {SpscStereoRing bad(0);}catch(const std::invalid_argument&){threw=true;}
    assert(threw);
    std::cout << "bridge tests PASS\n";
}
