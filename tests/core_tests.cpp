#include "modern_asio/spsc_stereo_ring.hpp"
#include "modern_asio/period_policy.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " << #expr << "\n"; \
    std::exit(1); } } while (false)

static void sequential_test() {
    using namespace modern_asio;
    SpscStereoRing q(8);
    const std::array<float, 12> a{1, 10, 2, 20, 3, 30, 4, 40, 5, 50, 6, 60};
    std::array<float, 16> out{};
    CHECK(q.capacity_frames() == 8);
    CHECK(q.push(a.data(), 6) == 6);
    CHECK(q.push(a.data(), 6) == 2);
    CHECK(q.available_frames() == 8);
    CHECK(q.pop(out.data(), 5) == 5);
    for (int i = 0; i < 10; ++i) CHECK(out[i] == a[i]);
    const std::array<float, 12> b{7, 70, 8, 80, 9, 90, 10, 100, 11, 110, 12, 120};
    CHECK(q.push(b.data(), 6) == 5);
    CHECK(q.pop(out.data(), 8) == 8);
    const std::array<float, 16> expected{6, 60, 1, 10, 2, 20, 7, 70, 8, 80, 9, 90, 10, 100, 11, 110};
    CHECK(out == expected);
    CHECK(q.pop(out.data(), 2) == 0);
    CHECK(q.available_frames() == 0);
    CHECK(q.push(nullptr, 2) == 0);
    bool caught = false;
    try { SpscStereoRing bad(9); } catch (const std::invalid_argument&) { caught = true; }
    CHECK(caught);
}
static void concurrency_test() {
    using namespace modern_asio;
    constexpr int kFrames = 200000;
    SpscStereoRing q(1024);
    std::thread producer([&] {
        std::array<float, 32> sample{};
        int sent = 0;
        while (sent < kFrames) {
            const int n = std::min(16, kFrames - sent);
            for (int i = 0; i < n; ++i) {
                sample[2*i] = static_cast<float>(sent + i);
                sample[2*i+1] = -static_cast<float>(sent + i);
            }
            const auto accepted = q.push(sample.data(), n);
            sent += static_cast<int>(accepted);
            if (!accepted) std::this_thread::yield();
        }
    });
    std::array<float, 64> output{};
    int received = 0;
    while (received < kFrames) {
        const auto got = q.pop(output.data(), std::min(32, kFrames - received));
        for (std::size_t i = 0; i < got; ++i) {
            CHECK(output[2*i] == static_cast<float>(received + static_cast<int>(i)));
            CHECK(output[2*i+1] == -output[2*i]);
        }
        received += static_cast<int>(got);
        if (!got) std::this_thread::yield();
    }
    producer.join();
    CHECK(received == kFrames);
    CHECK(q.available_frames() == 0);
}
static void period_test() {
    using namespace modern_asio;
    SharedPeriodRange p{480, 48, 96, 480};
    CHECK(choose_shared_period(p, 48000, 0) == 480);
    CHECK(choose_shared_period(p, 48000, 3) == 144);
    CHECK(choose_shared_period(p, 48000, 1) == 96);
    CHECK(choose_shared_period(p, 48000, 15) == 480);
    CHECK(choose_shared_period({480, 7, 100, 450}, 48000, 0) == 0);
    CHECK(choose_shared_period({480, 7, 100, 450}, 48000, 4) == 196);
    CHECK(choose_shared_period({480, 0, 100, 480}, 48000, 4) == 0);
}
int main() {
    sequential_test();
    concurrency_test();
    period_test();
    std::cout << "core tests: PASS\n";
}
