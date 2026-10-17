# ModernSharedASIO roadmap / handoff (2026-10-18)

- [x] M0: initialize repository, architectural decisions, portable FIFO/period policy and locally passing tests.
- [x] M1-source: WASAPI shared-only render backend source, IAudioClient3 with safe shared fallback, MMCSS events, stop/join, diagnostic CLI.
- [ ] M1-Windows: build Debug/Release x64 with Windows SDK, run smoke with YouTube and multiple devices; inspect HRESULT, engine period, underruns and CPU.
- [ ] M2: implement Steinberg-ASIO-SDK-compatible IASIO 24-slot interface, independent COM class factory, unique CLSID, registry registration, x64 DLL; do not overwrite original driver.
- [ ] M3: connect ASIO host double-buffer callbacks to FIFO, timing, clocks, interleaved conversion, correct start/stop and errors; at least stereo playback.
- [ ] M4: recording/loopback as needed, device selection and changes, accurate latencies, sample-rate negotiation or resampling, channel mapping and reset notifications.
- [ ] M5: Cubase 8.0.40 + YouTube runtime, long stability, original/new A/B xrun/CPU/latency tests, package/uninstall and final CI.

**Status discipline:** SOURCE_WRITTEN vs BUILD_VERIFIED vs WINDOWS_RUNTIME_VERIFIED vs CUBASE_VERIFIED. The latter two have not occurred; there is no installable ASIO DLL yet.

**Testing cadence:** local lightweight unit tests during batches, CI only at significant Windows driver validation gate. No GitHub Actions added yet.

Reference (read-only) https://github.com/skatmdgn/ASIOGLLD
