#include "modern_asio/host_pcm_tracker.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);std::abort();} } while(false)
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
using modern_asio::HostPcmSnapshot;
static void legacy(HostPcmSnapshot& s,const float* left,const float* right,
                   std::size_t frames,std::uint64_t ord) {
    bool nl=false,nr=false,invalid=false;
    float pl=0,pr=0;
    for(std::size_t f=0;f<frames;++f) {
        if(left) {
            const auto v=left[f];
            if(!std::isfinite(v))invalid=true;
            else {
                const auto a=std::fabs(v);
                if(a>pl)pl=a;
                if(a>0.000001f)nl=true;
            }
        }
        if(right) {
            const auto v=right[f];
            if(!std::isfinite(v))invalid=true;
            else {
                const auto a=std::fabs(v);
                if(a>pr)pr=a;
                if(a>0.000001f)nr=true;
            }
        }
    }
    if(!nl && !nr)++s.silent_host_blocks;
    else {
        if(s.first_nonzero_block==UINT64_MAX)s.first_nonzero_block=ord;
        s.last_nonzero_block=ord;
    }
    if(nl)++s.nonzero_left_blocks;
    if(nr)++s.nonzero_right_blocks;
    if(invalid)++s.invalid_float_blocks;
    const auto ppm=[](float p){return std::uint64_t(double(std::min(p,1000.0f))*1000000.0);};
    s.max_left_peak_ppm=std::max(s.max_left_peak_ppm,ppm(pl));
    s.max_right_peak_ppm=std::max(s.max_right_peak_ppm,ppm(pr));
}
static bool equal(const HostPcmSnapshot&a,const HostPcmSnapshot&b) {
    return a.silent_host_blocks==b.silent_host_blocks&&
        a.first_nonzero_block==b.first_nonzero_block&&
        a.last_nonzero_block==b.last_nonzero_block&&
        a.nonzero_left_blocks==b.nonzero_left_blocks&&
        a.nonzero_right_blocks==b.nonzero_right_blocks&&
        a.invalid_float_blocks==b.invalid_float_blocks&&
        a.max_left_peak_ppm==b.max_left_peak_ppm&&
        a.max_right_peak_ppm==b.max_right_peak_ppm;
}
int main(){
    modern_asio::HostPcmTracker t;
    HostPcmSnapshot expected{};
    std::array<float,512> l{},r{};
    std::mt19937 rng(440019);
    for(std::uint64_t b=0;b<15000;++b) {
        for(std::size_t i=0;i<512;++i){
            const auto v=static_cast<int>(rng()%400000)-200000;
            l[i]=float(v)/1234567.0f;
            r[i]=-l[i];
        }
        if(b%11==0)l.fill(0.f);
        if(b%13==0)r.fill(0.f);
        if(b%17==0)l[b%512]=std::numeric_limits<float>::quiet_NaN();
        if(b%19==0)r[b%512]=std::numeric_limits<float>::infinity();
        if(b%23==0)l[b%512]=1e-7f;
        const float* left=b%5==0?nullptr:l.data();
        const float* right=b%7==0?nullptr:r.data();
        legacy(expected,left,right,512,b);
        t.observe(left,right,512,b);
        CHECK(equal(expected,t.snapshot_stopped()));
    }
    t.reset_stopped();
    CHECK(equal(t.snapshot_stopped(),HostPcmSnapshot{}));
    std::cout<<"Host PCM single-writer aggregate equivalence PASS\n";
}
