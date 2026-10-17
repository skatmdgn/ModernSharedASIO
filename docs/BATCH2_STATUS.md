# Batch 2 — experimental ASIO COM adapter and correctness patches

Date: 2026-10-18. Repository: skatmdgn/ModernSharedASIO. **No native Windows build or Cubase runtime check has run.**

## Implemented in source (NOT an installable release)

- 24-method IASIO 2.3 virtual interface class and IClassFactory, x64 COM DLL exports, independent CLSID `{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}` and explicit regsvr32 registration functions.
- QueryInterface supports IID_IUnknown and its actual CLSID. Steinberg's ASIO host uses CoCreateInstance with the driver CLSID as class and interface ID.
- Stereo playback MVP: two noninterleaved Float32 ASIO output buffers, 512-frame block, output pre-roll, callback dispatch, allocation-free stereo bridge to Windows shared-mode renderer.
- WASAPI: IAudioClient3 with conservative period choice and shared IAudioClient fallback; never exclusive mode.
- COM scope lifetime: dependent IMMDevice and IAudioClient interfaces are released before CoUninitialize.
- Correct 24-valid-bit PCM in 32-bit container by leaving 8 low padding bits.
- SPSC buffer validates dimensions before allocation; clear_stopped requires both producer and consumer stopped.
- Portable unit tests for buffers, channel mapping, wrapped writes and overflow accounting. Linux Debug/Release only.

## Blockers (release gates)

1. Windows 11/10 x64 Visual Studio 2022 with Windows SDK: compile `MSA_BUILD_ASIO_DLL=ON` against official ASIO SDK and fix any build errors.
2. Validate COM factory/registry/CLSID in a test setup. Keep existing Steinberg DLL installed and untouched. Only register experimental DLL deliberately when approved.
3. Callback scheduling is **not synchronized to the WASAPI IAudioClock**, using a separate std::chrono timer. Drift and underruns are foreseeable. This is a prototype, not reliable realtime audio yet.
4. Recording, sample-rate conversion, hot-plug, output channel layout mapping, device-accurate latency reporting and robust shutdown under stalled host callbacks are incomplete.
5. Licensing: official Steinberg ASIO SDK is dual licensed. Select appropriate license before distributing SDK-derived binaries; respect trademark rules.
6. Run identical Cubase 8.0.40 + YouTube performance tests against the original asioglld.dll. No performance claims before this.

## Build outline — Windows x64

```powershell
git clone https://github.com/audiosdk/asio C:\sdk\asio
cmake -S . -B build -A x64 -DMSA_BUILD_ASIO_DLL=ON -DASIO_SDK_DIR=C:/sdk/asio
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

**Do not register or load this DLL in Cubase yet.** Native Windows build validation and COM smoke tests are required first. CI deferred to final Windows validation.
