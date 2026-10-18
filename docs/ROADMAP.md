# ModernSharedASIO roadmap / session handoff (2026-10-18)

**Purpose:** Windows x64 shared-mode audio output for Cubase 8.0.40 with YouTube simultaneous playback. **Existing Steinberg DLL must never be overwritten.**

## Actual state

- [x] M0 — repository initialized, architecture, evidence crosswalk and core FIFO/period-policy source.
- [x] M1 source — IAudioClient3/shared-only WASAPI renderer, MMCSS, output diagnostic CLI, error/underrun counters.
- [x] M2 source — experimental 24-method IASIO interface, independent COM class factory and CLSID, exports/registration code, registry-free COM preflight program.
- [x] M3 portable source — ASIO Float32 planar→interleaved stereo bridge, preallocated FIFO, buffer wrap/overflow tests.
- [x] M3 prototype — ASIO callbacks and double-buffer producer attached to WASAPI render worker; no independent periodic software timer.
- [ ] M1–M6 Windows build — Debug/Release x64 Windows SDK + external official ASIO SDK, fix compile/link errors, registry-free COM preflight and shared-mode smoke test.
- [x] M3 event pacing — ASIO blocks produced only when WASAPI output demands frames; bounded requests/no-progress guard.
- [x] M3 simulation — 60,000 × 441-frame and 60,000 × 480-frame consumption events, verified conservation with sanitizers.
- [x] M3 clock diagnostics source — periodic IAudioClock position, clock frequency and paired QPC (100ns units).
- [x] Batch 4: isolate startup priming from Cubase host callbacks, atomic stream active state, failure rollback, producer-failure counter and startup unit test.
- [x] Batch 5 source: separate event-driven ASIO producer/MMCSS thread from the WASAPI consumer; bound buffering to two ASIO blocks; signal producer after render packet submission.
- [x] Batch 5 tests: 60,000 randomized bounded-prefill inputs PASS under Clang ASan/UBSan; 60,000-event 44.1/48k SPSC pacing simulations pass under sanitizers; CMake now registers seven portable tests (full suite NOT rerun).
- [ ] Batch 5 runtime: prove producer/consumer wakeups on Windows, measure audio deadlines, callback lifetimes, underruns and added buffering latency.
- [x] Batch 6 source — correlated clock position + QPC observation channel, exact ticks-to-frames and 100ns-to-ns conversion with overflow guards, and last-stream-HRESULT telemetry.
- [x] Batch 6 portable tests — 100,000 random exact-arithmetic comparisons and 400,000-publish concurrent coherent snapshot test, with sanitizers and optimized Release.
- [ ] M3 clock accuracy — correlate rendered device position with ASIO sample position and timestamp in physical Windows execution; measure real latency, frame offset and any sample discontinuity.
- [ ] M4 — accurate latency and time info, device enumeration/restart, xrun recovery, capture support, rate negotiation/resampling, channel layout.
- [ ] M5 — Cubase 8.0.40 + YouTube actual Windows A/B testing vs old DLL, memory/thread stress, release packaging, final CI.

## Verification discipline

- Portable clock tests: Linux Clang Debug ASan+UBSan and Release passed. Full registered CTest suite was not rerun. Native Windows build is still unavailable in this environment.
- Windows compilation: **NOT DONE**, Windows runtime: **NOT DONE**, Cubase 8: **NOT DONE**.
- No real-world audio improvement can be claimed yet. Source-written does NOT mean deployable.

## Windows build (NOT validated)

```powershell
git clone https://github.com/audiosdk/asio C:\sdk\asio
cmake -S . -B build-win -A x64 -DMSA_BUILD_ASIO_DLL=ON -DASIO_SDK_DIR=C:/sdk/asio
cmake --build build-win --config Debug
ctest --test-dir build-win -C Debug --output-on-failure
.\build-win\Debug\modern_asio_preflight.exe .\build-win\Debug\ModernSharedASIO.dll
.\build-win\Debug\modern_asio_smoke.exe
```

**Do not register or load the experimental DLL in Cubase yet**; run preflight and basic WASAPI tests first.

## Workflow

Lightweight local tests at each meaningful batch, record SOURCE_WRITTEN / BUILD_VERIFIED / WINDOWS_RUNTIME_VERIFIED / CUBASE_VERIFIED separately. CI only at final Windows validation gate. No GitHub Actions yet.

Original reverse engineering reference (read-only): https://github.com/skatmdgn/ASIOGLLD
