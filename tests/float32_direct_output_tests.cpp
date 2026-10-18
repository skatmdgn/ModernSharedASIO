// Differential test for native Float32 stereo WASAPI transfer.
// The former path was FIFO -> scratch -> memcpy to device; new path writes
// directly into device and must produce identical bytes even on underrun.
#include "modern_asio/float32_output_transfer.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();} } while(false)
static void run_case(std::size_t capacity) {
    modern_asio::SpscStereoRing old_fifo(capacity),new_fifo(capacity);
    std::mt19937_64 rng(0xace000 + capacity);
    std::vector<float> source(capacity*2),scratch(capacity*2);
    std::vector<float> old_dst(capacity*2+2),new_dst(capacity*2+2);
    const std::size_t bound=std::min<std::size_t>(capacity,2048);
    for(int iteration=0;iteration<10000;++iteration) {
        const auto inserted=(iteration%99==0)?capacity:static_cast<std::size_t>(rng()%(bound+1));
        for(std::size_t i=0;i<inserted*2;++i)
            source[i]=static_cast<float>(rng()%1000000)/277777.0f;
        CHECK(old_fifo.push(source.data(),inserted)==new_fifo.push(source.data(),inserted));
        const auto frames=(iteration%67==0)?capacity:static_cast<std::size_t>(rng()%(bound+1));
        old_dst[0]=new_dst[0]=123.5f;
        old_dst[frames*2+1]=new_dst[frames*2+1]=-42.75f;
        const auto n=old_fifo.pop(scratch.data(),frames);
        if(n<frames)std::fill_n(scratch.data()+n*2,(frames-n)*2,0.0f);
        if(frames)std::memcpy(old_dst.data()+1,scratch.data(),frames*2*sizeof(float));
        const auto r=modern_asio::fill_native_stereo_float32(new_fifo,new_dst.data()+1,frames);
        CHECK(r.copied_frames==n && r.silence_frames==frames-n);
        CHECK(std::memcmp(old_dst.data(),new_dst.data(),
                          (frames*2+2)*sizeof(float))==0);
        CHECK(old_fifo.available_frames()==new_fifo.available_frames());
    }
}
static void large_device_chunk_test() {
    modern_asio::SpscStereoRing fifo(512);
    std::vector<float> source(1024,0.375f),output(2304,1.0f);
    CHECK(fifo.push(source.data(),512)==512);
    std::size_t offset=0;
    while(offset<1152) {
        const auto count=std::min<std::size_t>(512,1152-offset);
        const auto r=modern_asio::fill_native_stereo_float32(
            fifo,output.data()+2*offset,count);
        CHECK(r.copied_frames==(offset==0?512u:0u));
        offset+=count;
    }
    for(std::size_t i=0;i<1152*2;++i)
        CHECK(output[i]==(i<512*2?0.375f:0.0f));
}
int main() {
    for(const auto cap:{2,8,16,512,1024,32768})run_case(cap);
    large_device_chunk_test();
    std::puts("native stereo float32 direct transfer: PASS (60000 differential cases)");
}
