# ModernSharedASIO

**Status: Batch 5 isolated ASIO producer prototype; Windows compilation and Cubase 8 tests NOT DONE. Do not install.**

Independent Windows x64 shared-mode driver experiment targeting **Cubase 8.0.40 + YouTube simultaneously**. The original driver and reverse engineering reference are at [ASIOGLLD](https://github.com/skatmdgn/ASIOGLLD); the proprietary DLL is not copied, modified or overwritten.

## Implemented in source

- SPSC lock-free stereo Float32 ring, period policy and deterministic core/bridge tests.
- ASIO Float32 planar output channel mapping into a bounded interleaved FIFO; overflow counted.
- WASAPI shared-only IAudioClient3 engine with fresh IAudioClient shared fallback, MMCSS and Windows audio event scheduling.
- Experimental 24-method IASIO 2.3 COM adapter, IClassFactory, own CLSID, explicit registration/unregistration exports, ASIO callback producer, 512-frame double-buffer output.
- 5-second shared-mode smoke executable and **registry-free COM factory preflight** executable.
- ASIO callback block generation driven by WASAPI render events, no independent callback sleep timer.
- Optional IAudioClock output diagnostics (device ticks, frequency, QPC timestamp in 100-ns units).
- Portable demand tests, startup callback isolation test, and 44.1/48k event-consumption simulations.
- Initial WASAPI priming bypasses Cubase callbacks; active-state protection covers startup, and producer failures are separately reported.
- Batch 5: move Cubase ASIO DSP callbacks to a separate MMCSS producer thread; WASAPI render worker only consumes the prefilled ring and posts a Win32 event to wake the producer.
- Producer fills at most 2×512 frames ahead, with at most four block requests per wake. This changes the scheduling/latency trade-off and is not yet validated on hardware.
- Fix missing CMake registration of the prior Batch 4 startup-isolation test; add 60,000-case randomized bounded-prefill policy unit test and 44.1/48k SPSC pacing simulation (idealized immediate wakeup).
- Corrected COM lifetime ordering and 24 valid bit / 32-bit PCM container conversion.

## Build

Portable tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows x64 (Visual Studio 2022 + Windows SDK + external official [ASIO SDK](https://github.com/audiosdk/asio)):

```powershell
cmake -S . -B build-win -A x64 -DMSA_BUILD_ASIO_DLL=ON -DASIO_SDK_DIR=C:/sdk/asio
cmake --build build-win --config Debug
ctest --test-dir build-win -C Debug --output-on-failure
.\build-win\Debug\modern_asio_preflight.exe .\build-win\Debug\ModernSharedASIO.dll
```

Windows x64 manual build and registry-free COM preflight helper: `scripts/build_windows_x64.ps1`. It does not install or register a driver.

No source or compiled binaries from the ASIO SDK are bundled. Refer to [SDK licensing and compatibility](docs/SDK_COMPATIBILITY.md).

## Important limitations

**Do not register the DLL or use it in Cubase yet**: Windows compilation and host/COM preflight are unverified. ASIO callbacks are now paced by WASAPI render-buffer demand. This is **not validated hardware clock synchronization**: device-clock sample-position mapping, measured latency, Cubase host callback timing and real underruns are still unresolved. Device loss, resampling, actual latency reporting, recording and installer also remain incomplete. Shared mode is the only implemented WASAPI mode; microphone/loopback capture not implemented.

See [roadmap](docs/ROADMAP.md), [Batch 2 status](docs/BATCH2_STATUS.md), [architecture](docs/ARCHITECTURE.md), and [original evidence](docs/ORIGINAL_EVIDENCE.md).
