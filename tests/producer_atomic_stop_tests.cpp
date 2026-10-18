#include <atomic>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);std::abort();} } while(false)
#include <chrono>
#include <cstdint>
#include <thread>
#include <iostream>
int main() {
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> callbacks{0};
    std::thread worker([&] {
        while(!stop.load(std::memory_order_acquire)) {
            callbacks.fetch_add(1,std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });
    while(callbacks.load(std::memory_order_acquire)<1000)
        std::this_thread::yield();
    stop.store(true,std::memory_order_release);
    worker.join();
    const auto fixed=callbacks.load(std::memory_order_acquire);
    CHECK(fixed>=1000);
    std::this_thread::yield();
    CHECK(callbacks.load(std::memory_order_relaxed)==fixed);
    stop.store(false,std::memory_order_release);
    CHECK(!stop.load(std::memory_order_acquire)); // driver start reset
    std::cout<<"Producer atomic stop request PASS\n";
}
