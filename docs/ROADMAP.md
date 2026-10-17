# ModernSharedASIO roadmap / session handoff (2026-10-18)

**Purpose:** Windows x64 shared-mode audio output for Cubase 8.0.40 with YouTube simultaneous playback. **Existing Steinberg DLL must never be overwritten.**

## Actual state

- [x] M0 — repository initialized, architecture, evidence crosswalk and core FIFO/period-policy source.
- [x] M1 source — IAudioClient3/shared-only WASAPI renderer, MMCSS, output diagnostic CLI, error/underrun counters.
- [x] M2 source — experimental 24-method IASIO interface, independent COM class factory and CLSID, exports/registration code, registry-free COM preflight program.
- [x] M3 portable source — ASIO Float32 planar→interleaved stereo bridge, preallocated FIFO, buffer wrap/overflow tests.
- [x] M3 experimental source — ASIO callbacks and double-buffer producer connected to WASAPI FIFO, **not hardware-clock synchronized**.
- [ ] M1–M3 Windows build — Debug/Release x64 Windows SDK + external official ASIO SDK, fix compile/link errors, registry-free COM preflight and shared-mode smoke test.
- [ ] M3 clock — drive ASIO callback cadence from WASAPI timing/position (or validated closed-loop controller); remove free-running sleep_until as production scheduler, measure drift.
- [ ] M4 — accurate latency and time info, device enumeration/restart, xrun recovery, capture support, rate negotiation/resampling, channel layout.
- [ ] M5 — Cubase 8.0.40 + YouTube actual Windows A/B testing vs old DLL, memory/thread stress, release packaging, final CI.

## Verification discipline

- Portable core tests: Linux Debug/Release/ASan+UBSan passed for newly added bridge tests in local sandbox.
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
