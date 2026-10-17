# ModernSharedASIO

**Status: early development, NOT an ASIO DLL. Do not install in Cubase yet.**

A new Windows x64 ASIO-to-WASAPI shared-mode driver project. Goal: **Cubase 8.0.40 and YouTube playing concurrently** with stable low latency. The previously reverse-engineered `skatmdgn/ASIOGLLD` is an **evidence and compatibility reference**, not proprietary code copied into this repository.

## Batch 1 implemented

- Cross-platform, preallocated SPSC stereo float ring; atomic indices, bounded producer/consumer operations, no allocations or locks in push/pop.
- Windows shared engine period policy: respect default, min, max, fundamental frames; default unless shorter period explicitly requested.
- Windows WASAPI shared-only renderer source: IAudioClient3 event-driven stream or fresh IAudioClient shared fallback, MMCSS audio worker, endpoint mix format conversion, stop-join cleanup, underrun/error counters.
- Windows five-second smoke executable to check simultaneous YouTube audio, silent by default or optional quiet 440Hz tone.
- Core tests including concurrent two-thread transfer of 200,000 frames.

## Build and tests

Portable tests (Windows, Linux, macOS):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows x64 with Visual Studio 2022 / Windows SDK:

```powershell
cmake -S . -B build-win -A x64
cmake --build build-win --config Debug
ctest --test-dir build-win -C Debug --output-on-failure
.\build-win\Debug\modern_asio_smoke.exe
# Optional quiet tone:
.\build-win\Debug\modern_asio_smoke.exe --tone --period-5ms
```

The diagnostic program opens **shared mode only**. It does not register an ASIO driver. The portable tests passed on Linux in Debug and Release. **The Windows backend has not been compiled or executed on Windows yet.** A successful silence playback alone is not Cubase validation.

## Important limitations

- Playback only; stereo source; excess device channels filled with silence.
- Source rate must match Windows device mix rate. No resampler or recording.
- Missing audio frames become silence and increment underrun count.
- Default eConsole output only; no automatic hot-plug recovery or setting persistence.
- **No COM class factory, IASIO implementation, Cubase DLL or installer yet.**
- Do not overwrite the Steinberg DLL; original binaries and disassembly are excluded from this repo.

See [architecture](docs/ARCHITECTURE.md), [evidence crosswalk](docs/ORIGINAL_EVIDENCE.md), [roadmap](docs/ROADMAP.md) and [test plan](docs/TEST_PLAN.md).
