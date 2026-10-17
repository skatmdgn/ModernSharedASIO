# Windows validation (NOT_YET_RUN)

1. Visual Studio 2022 + Windows SDK x64 Debug/Release build; run CTest for both. Never rely on debug-only `assert` tests for Release.
2. `modern_asio_smoke.exe` default: confirm mix rate/channel/buffer, IAudioClient3 selection, event and underflow counts; concurrently play a YouTube video. Silence from the smoke app is expected.
3. `--tone` at low listening volume: check both 440Hz -36 dBFS tone and browser audio can play.
4. Repeat `--period-3ms` and `--period-5ms`; log whether the engine accepts the period or shared fallback is used.
5. Inspect device formats mono, stereo, multichannel, 16/24/32 PCM and float32; assert conversions and channel layout.
6. Change/disconnect default audio device midstream: expect noncrashing error and clean shutdown. Automatic re-open not implemented.
7. Use ETW/WPR to record scheduling, CPU, glitches, missed deadlines, underruns. Compare against original only with identical device/settings.
8. Future ASIO DLL: Cubase 8.0.40 can enumerate unique driver, init, create/dispose buffers, query sample rate/latency, play while YouTube plays, reset/restart and close safely.
9. Soak >= 30min and compare original/new underruns, CPU and measured loopback latency before performance claims.

Any unexecuted item stays NOT_TESTED rather than PASS.

10. Run `modern_asio_clock_sim_tests` to validate frame conservation for 44.1kHz and 48kHz *simulated* 10ms events. These results are never proof of actual Windows performance.
11. Log IAudioClock device position, GetFrequency and QPC (100-ns units) from smoke output. Correlate against actual render events and underruns in Windows before claiming drift/latency improvements.
12. Check that the ASIO callback directly on the WASAPI render worker does not block the device deadline on heavy Cubase projects. If it does, implement a bounded, clock-paced producer rather than reinstate a free-running timer.

13. Validate startup callback isolation; first priming never invokes Cubase producer. A later real WASAPI device event may race outer ASIO start return and needs host validation.
14. Record producer_failures separately from stream_errors and underrun_frames; startup zero padding is intentional, not a real runtime xrun.
15. Use scripts/build_windows_x64.ps1 for Windows x64 build and registry-free COM preflight, never automatic driver registration.
