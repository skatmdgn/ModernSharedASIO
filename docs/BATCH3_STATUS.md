# Batch 3 — WASAPI-event-paced ASIO prototype

Date 2026-10-18. Current repository: skatmdgn/ModernSharedASIO main. Neither Windows build nor Cubase runtime has been verified.

## Concrete changes

- Removed independent std::chrono callback thread and its sleep_until loop from the IASIO driver.
- Generated noninterleaved 512-frame Float32 ASIO output blocks only when the WASAPI render worker needs frames. Shared FIFO memory is preallocated.
- Added a bounded `fulfill_audio_demand` helper; it stops if the producer fails or makes no progress, preventing a real-time busy loop.
- The render worker prepares PCM data before IAudioRenderClient::GetBuffer and dispatches ASIO host callbacks outside driver mutexes.
- Added an explicit render-thread self-join guard and linked advapi32 for Windows registry exports.
- Counted the initial ASIO pre-roll block in generated sample position; acquired MMCSS priority before first ASIO host callback.
- Added optional IAudioClock::GetFrequency and GetPosition diagnostic counters every eighth render event; QPC values are in 100ns units. This is measurement instrumentation, not sample-position calibration.
- Added a 44.1k/48k 60,000-event frame conservation simulation and bounded-demand tests.

## Verification

Portable demand tests and simulations were compiled locally with Clang C++20, -Wall/-Wextra/-Wpedantic/-Werror and AddressSanitizer+UndefinedBehaviorSanitizer, and passed. The 44.1k simulator consumed 26,460,000 frames; the 48k simulator consumed 28,800,000 frames; generated ASIO blocks exactly covered consumption with remaining FIFO frames below one ASIO block.

These tests use simulated demand and Linux portable code. They do not compile the Windows WASAPI code or the ASIO SDK COM adapter. No Windows compiler/SDK headers are installed here; Windows Debug/Release build, COM preflight, driver registration and Cubase 8.0.40 testing remain NOT_TESTED. GitHub Actions/CI intentionally deferred.

## Important limitations / next batch

1. Build for Windows x64 against official Steinberg ASIO SDK, resolve compiler/linker issues, then run registry-free COM preflight.
2. Test eConsole shared-mode output alongside YouTube. Observe WASAPI event cadence, IAudioClock, stream errors and underflows.
3. A slow Cubase plug-in callback running on the WASAPI worker can starve the render deadline. Profile and, if needed, decouple via a bounded, device-clock-paced producer.
4. ASIO sample position and ASIOTime timestamps are not yet calibrated to device-played frames or real output latency. Input/capture, hotplug and sample-rate conversion remain incomplete.
5. Original Steinberg DLL and ASIOGLLD are unmodified; never overwrite existing installation.

## External API references

- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition
- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream
