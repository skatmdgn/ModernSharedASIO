# Batch 5 — separate ASIO producer and WASAPI render worker

Date: 2026-10-18. Current state: source written; Windows x64 build and Cubase 8.0.40 run NOT TESTED.

## Driver architecture change

Earlier MSA had a single WASAPI MMCSS render worker that synchronously executed Cubase's bufferSwitch or bufferSwitchTimeInfo. Under a slow plugin callback the Windows audio event deadline could be missed. Batch5 instead introduces a second event-driven producer worker:

- Producer worker: independent std::thread using Win32 stop (manual-reset) and wake (auto-reset) events; optionally applies MMCSS Pro Audio / Audio task priority NORMAL. Only this thread calls Cubase host callbacks.
- Windows render worker: consumes only ready SPSC audio, then after successful IAudioRenderClient::ReleaseBuffer calls a lightweight source callback that only invokes SetEvent. It never synchronously runs host DSP.
- Buffer policy: use purely arithmetic producer_blocks_to_fill to refill to at most two 512-frame ASIO blocks (1,024 frames), bounded by queue capacity and four blocks per event. Wake signals coalesce. Source is single producer, WASAPI event worker single consumer.
- Startup: original prefilled ASIO B-half is queued; worker starts before Windows audio initialization; source callback does not fire during intentional initial priming; producer is notified after shared engine reports ready.
- Shutdown: stop WASAPI renderer first; signal/stop producer, join its thread, then close its event handles. Rejected self-join on render or producer thread.
- Failed initialization or sample rate mismatch stops both workers without freeing producer resources while active.

## Testing and evidence

- New pure bounded producer fill policy tested locally using Clang C++20, AddressSanitizer and UndefinedBehaviorSanitizer. 60,000 randomized cases passed, including edge/overflow cases.
- Review found that Batch4 startup_isolation_tests.cpp existed but CMake did not register it; Batch5 CMake now registers it. The previous '5/5 CTest' statement was not backed by that actual CMake file and must not be repeated without a real new full-suite run.
- No Windows SDK headers, MSVC or MinGW SDK are installed in current environment. clang-cl is present but does NOT provide Windows headers/libraries. Windows DLL and ASIO host behavior NOT VERIFIED.
- No CI or GitHub Actions executed; existing Steinberg DLL and ASIOGLLD repository unchanged.

## Known residual risks

1. On live device demand the render thread may see an empty ring and output silence; target=1024 frames is an uncalibrated latency-versus-safety choice. Existing ASIO sample position/QPC timestamps are not device-clock calibrated.
2. A Cubase host callback that blocks can stall producer shutdown; stop() intentionally joins rather than freeing still-referenced memory. Design a bounded host-callback lifetime protocol before release.
3. Host Release() during its own callback may destroy Driver on the producer thread; self-join check is NOT sufficient for safe destruction. COM object lifetime must be hardened and exercised on Windows.
4. Clock drift is reduced by event-triggered producer demand, but actual callback latency, scheduling jitter and frame-to-device timestamp relation remain unmeasured.
5. No input/recording, sample-rate converter, hotplug recovery or advanced multichannel mapping yet.

## Next concrete gate

Get a Windows x64 MSVC + Windows SDK build with external official Steinberg ASIO SDK; run registry-free COM preflight, then a shared mode smoke test, and only afterward cautiously test Cubase 8.0.40 plus browser audio. Do not register/install the DLL at this point.

Code paths: src/asio_com_driver.cpp, src/wasapi_shared_renderer.cpp, include/modern_asio/producer_fill_policy.hpp, tests/producer_fill_policy_tests.cpp.

## Additional portable integration test

- `tests/producer_pacing_sim_tests.cpp` now uses the **same SpscStereoRing class** as the driver, a prefilled 512-frame B-half, and a simulated producer activation after each WASAPI consumption.
- 60,000 nominal 10ms events at 44.1 kHz: 26,460,000 frames consumed; 26,461,184 produced; 1,184 queued.
- 60,000 nominal 10ms events at 48 kHz: 28,800,000 consumed; 28,801,024 produced; 1,024 queued.
- Local Clang17 C++20 with ASan+UBSan passed; `producer_fill_policy_tests.cpp` also passed in optimized `-O2 -DNDEBUG` mode.
- **Idealized limitation:** the simulated producer wakes immediately after each event. This does not model Windows scheduler latency, host callback execution time, MMCSS failures, or audio device resets. It does NOT prove glitch-free sound.
- CMake now registers **seven** portable tests, including Batch4 startup isolation (previously unregistered). Only new Batch5 policy/pacing tests were executed in this turn; do not claim that all seven ran.
