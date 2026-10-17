#ifdef _WIN32
#include "modern_asio/wasapi_shared_renderer.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    bool play_tone = false;
    std::uint32_t period = 0;
    for (int i = 1; i < argc; ++i) {
        std::wstring_view arg(argv[i]);
        if (arg == L"--tone") play_tone = true;
        else if (arg == L"--period-3ms") period = 3;
        else if (arg == L"--period-5ms") period = 5;
        else {
            std::wcerr << L"Usage: modern_asio_smoke [--tone] [--period-3ms|--period-5ms]\n";
            return 2;
        }
    }
    modern_asio::SpscStereoRing audio(32768);
    modern_asio::WasapiSharedRenderer device(audio);
    auto hr = device.start(period);
    if (FAILED(hr)) {
        std::cerr << "WASAPI shared initialization failed: HRESULT 0x" << std::hex
                  << static_cast<unsigned long>(hr) << "\n";
        return 1;
    }
    const auto info = device.device_info();
    std::cout << "SHARED ONLY: rate=" << info.sample_rate << " Hz, channels=" << info.channels
              << ", buffer=" << info.buffer_frames << " frames, engine_period="
              << info.period_frames << ", IAudioClient3=" << info.iaudio_client3
              << ", reported_latency_100ns=" << info.stream_latency_100ns << "\n";
    std::cout << (play_tone ? "Quiet 440Hz tone for 5 seconds\n" :
                              "Silence for 5 seconds; play YouTube alongside to verify coexistence\n");
    const auto frames_per_tick = std::max(1u, info.sample_rate / 100);
    std::vector<float> buffer(static_cast<std::size_t>(frames_per_tick) * 2, 0);
    constexpr double pi = 3.14159265358979323846;
    std::uint64_t phase_frames = 0;
    auto next = std::chrono::steady_clock::now();
    for (int t = 0; t < 500; ++t) {
        if (play_tone) {
            for (std::uint32_t f = 0; f < frames_per_tick; ++f) {
                // -36 dBFS; play at a modest listening volume.
                const auto value = static_cast<float>(0.0158489 *
                    std::sin(2 * pi * 440.0 * static_cast<double>(phase_frames++) / info.sample_rate));
                buffer[static_cast<std::size_t>(f) * 2] = value;
                buffer[static_cast<std::size_t>(f) * 2 + 1] = value;
            }
        }
        audio.push(buffer.data(), frames_per_tick);
        next += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next);
    }
    device.stop();
    const auto stats = device.stats();
    std::cout << "render_events=" << stats.render_events << " underrun_frames="
              << stats.underrun_frames << " stream_errors=" << stats.errors
              << " producer_failures=" << stats.producer_failures << "\n";
    std::cout << "clock_reads=" << stats.clock_reads
              << " device_ticks=" << stats.clock_device_position
              << " frequency=" << stats.clock_frequency
              << " qpc_100ns=" << stats.clock_qpc_100ns << "\n";
    return stats.errors ? 1 : 0;
}
#endif
