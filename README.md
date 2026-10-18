# ModernSharedASIO

**Status: Windows x64 Debug DLL compiled and 9/9 tests passed; user confirmed audible standalone WASAPI output alongside YouTube (48kHz IAudioClient3, 500 render events, zero stream errors). ASIO legacy bufferSwitch playback was also user-verified concurrently with YouTube. ASIO TimeInfo mode and Cubase 8 remain UNTESTED. Do not install yet.**

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
- Batch 6: coherent, paired IAudioClock device position / frequency / QPC (100ns) snapshots, exact overflow-checked conversion to stream frames / ASIO nanoseconds and submitted-versus-played diagnostic.
- Store last renderer shutdown HRESULT for actual Windows runtime troubleshooting (e.g. audio event timeout and device invalidation).
- Clock conversion validated on local Linux using 100,000 randomized exact 128-bit arithmetic references plus 400,000 concurrent clock publications. These are **portable tests only**.
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

**Do not register the DLL or use it in Cubase yet**: Windows Debug compilation and registry-free COM factory preflight passed (9/9 tests), but actual Windows playback and Cubase host behavior have not yet been verified. ASIO callbacks are now paced by WASAPI render-buffer demand. This is **not validated hardware clock synchronization**: device-clock sample-position mapping, measured latency, Cubase host callback timing and real underruns are still unresolved. Device loss, resampling, actual latency reporting, recording and installer also remain incomplete. Shared mode is the only implemented WASAPI mode; microphone/loopback capture not implemented.

See [roadmap](docs/ROADMAP.md), [Batch 6 status](docs/BATCH6_STATUS.md), [Batch 2 status](docs/BATCH2_STATUS.md), [architecture](docs/ARCHITECTURE.md), and [original evidence](docs/ORIGINAL_EVIDENCE.md).

## Windows manual validation progress (2026-10-18)

The user's VS2022/MSVC x64 Debug build produced `build-win/Debug/ModernSharedASIO.dll`. All nine CTests passed, including registry-free COM object creation. Four `LNK4104` export warnings are nonfatal. The next step is to run the **standalone shared WASAPI smoke executable**, **without installing or registering the DLL**:

```powershell
cd C:\ModernSharedASIO
.\build-win\Debug\modern_asio_smoke.exe --tone
```

Test again while YouTube is already playing, then share the printed stream counters. See [verified Windows build log](docs/WINDOWS_BUILD_LOG.md). Do not claim Cubase support or audio improvements from the successful build alone.

### New manual registry-free ASIO output smoke

The ordinary WASAPI smoke produced a quiet audible tone while the user's YouTube audio played, with 960 missing input frames during the five-second test but zero WASAPI stream errors. This does NOT prove ASIO host output; the next test is an isolated mock ASIO host that loads the new DLL directly, without registration:

```powershell
git pull --ff-only
.\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug
.\build-win\Debug\modern_asio_host_smoke.exe .\build-win\Debug\ModernSharedASIO.dll --tone
# Optional alternative ASIO 2.x callback path:
.\build-win\Debug\modern_asio_host_smoke.exe .\build-win\Debug\ModernSharedASIO.dll --tone --time-info
```

This executable is not part of automatic CTest; it is deliberately a manual test requiring an actual audio endpoint. Do not use regsvr32 or alter the original Steinberg driver. The source is new and has not yet been compiled by MSVC.

### First registry-free IASIO playback confirmed on Windows

User has now run `modern_asio_host_smoke.exe ... --tone` and confirms simultaneous audio with YouTube. Actual stdout:

```text
Registry-free ASIO host: rate=48000, block=512, channels=2, mode=bufferSwitch
Quiet 440Hz ASIO tone for 5 seconds
ASIO callbacks=472 invalid_buffers=0 sample_position_low=242176
Registry-free ASIO host smoke: PASS
```

472 callback blocks × 512 frames + 512 prefilled frames = 242176, matching the reported sample position. This confirms **registry-free ASIO host playback via legacy bufferSwitch and WASAPI shared renderer** on the user's Windows machine. It does **not** establish Cubase compatibility, driver registration correctness, ASIO time-info callback operation, or real latency performance. Next manual check:

```powershell
.\build-win\Debug\modern_asio_host_smoke.exe .\build-win\Debug\ModernSharedASIO.dll --tone --time-info
```

No rebuild is needed for the second host smoke mode when the new host executable already exists. Do not register/install before this additional callback-path check and supervised registration procedure.

### ASIO TimeInfo Windows smoke passed; next Cubase opt-in

User confirmed second real Windows ASIO 2.x TimeInfo mode output with YouTube concurrently: 471 callbacks, 0 invalid buffers, 241664 sample position. Both callback modes are now validated using the standalone mock ASIO host (not Cubase).

Before Cubase 8.0.40 evaluation, opt-in reversible x64 installation instructions are in `scripts/register_cubase_test.ps1`, `scripts/unregister_cubase_test.ps1`, and `docs/WINDOWS_BUILD_LOG.md`. They target only an independent new COM CLSID / ASIO registration, and will stop rather than overwrite existing entries. The source scripts have not yet been run on Windows. Close Cubase and review the scripts before executing with Administrator privileges. The original `asioglld.dll` is not modified.

## Cubase VSTi regression: diagnostics in progress

Cubase 8.0.40 loads Modern Shared Audio and plays WAV audio, but VST instruments glitch, Serum may be silent and output latency is high. A new diagnostic build requests 5ms WASAPI shared periods where supported and writes callback timings and WASAPI underruns to %TEMP%\ModernSharedASIO-debug.log after IASIO::stop. An optional per-user PeriodMs override allows A/B comparison with the earlier Windows default period. This is **not yet built/tested on Windows**; users should use their old reliable ASIO driver for important work. See docs/CUBASE_DIAGNOSTICS.md.
