# ModernSharedASIO

**Status: Windows x64 Debug DLL compiled and 9/9 tests passed; user confirmed audible standalone WASAPI output alongside YouTube (48kHz IAudioClient3, 500 render events, zero stream errors). ASIO legacy bufferSwitch playback was also user-verified concurrently with YouTube. ASIO TimeInfo mode passed; Cubase 8 recognizes the driver and plays WAVs but VSTi performance/latency currently FAILS. Do not use for production.**

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

### ASIO 2.3 timeline bug fix / Cubase host samples diagnosis

The ASIO SDK requires **samplePosition 0 at the first ASIO bufferSwitch after start**. Our earlier driver erroneously reported **512**, conflating driver pre-roll of output buffer B with host callback progression. New AsioCallbackTimeline corrects this distinction, while the registry-free mini-host now asserts positions and double-buffer ordering. MSVC Windows build and Serum retest are PENDING. User logs showed actual WASAPI engine remained 10ms despite requesting 5ms and had zero underruns in the two longer failing VST runs. New stop-time host sample amplitude metrics and a reversible DisableTimeInfo registry A/B setting help isolate further defects without random latency tuning. See [ASIO timeline correction](docs/ASIO_TIMELINE_FIX.md).

### Multi-track Cubase displacement: ASIOOutputReady contract corrected (pending Windows retest)

User reports normal one-track output but greater delays, VST dropout and apparent ~16-bar timeline displacement as track count increases. A concrete ASIO SDK contract bug was found: our IASIO::outputReady() reported ASE_OK despite **not** implementing the asynchronous host-ready handshake. Fixed to ASE_NotPresent, and added COM/ASIO-host checks. This fix has **not yet been Windows built or Cubase verified**, and the 16-bar offset may have other causes. See [multi-track investigation](docs/CUBASE_MULTITRACK_OUTPUTREADY.md). Do not use the experimental driver for production.

### Cubase regression still unresolved after ASIOOutputReady change

User confirmed no improvement; recent logs show zero WASAPI underruns, 10ms actual shared period, with separate all-silent and part-nonzero host PCM sessions. Silence counters are whole-session aggregates (not proof of transport-time silence). The source has a potential unsynchronized `ASIOTimeInfo.systemTime`/physical device clock relationship and a host-reported latency change at renderer start. Next, isolate **ASIO-Guard OFF** then separately **TimeInfo OFF** using an existing HKCU diagnostic switch (no rebuild) before changing the engine again. See docs/CUBASE_CAUSAL_DIAGNOSIS.md. Current driver remains experimental and unusable for reliable multi-track Cubase work.

### Future material audible at empty start of Cubase arrangement — still unresolved

User explicitly describes starting playback at bar 0 with no clips there, but hearing material located roughly 16 bars into the project. This is **musical content at the wrong playhead position**, not conventional 10–50ms latency. The experimental driver now defaults to the legacy ASIO bufferSwitch path because its previous ASIO TimeInfo clock/sample association is uncalibrated; timeInfo can be explicitly reenabled for diagnostics. A distinct 16-bar-silence then bar17-marker FIFO content ordering test and callback-audio-vs-wall-time metrics were added. This is **source-written only**, not tested in Cubase yet and NOT a confirmed fix. Read [future-bar diagnosis](docs/CUBASE_FUTURE_BAR_FIX.md). For real music work, use the established ASIO driver.

## Whole-source audit for future-bar Cubase output (2026-10-18)

The user hears source music located ~16 bars into the arrangement when transport begins at a genuinely empty bar 0. A static audit found a definite time-pair contract defect: getSamplePosition returned a new QPC timestamp with an unchanged block sample position; the official ASIO 2.3 SDK requires a coherent pair and Windows timeGetTime-derived nanoseconds. Added atomically latched ASIO block stamps with mini-host equality checks, and zeroed reused ASIO planar halves before calling Cubase to avoid stale PCM. Both new portable tests passed local Clang ASan/UBSan. The SPSC FIFO holds only 32768 frames (~0.683s at 48kHz), not 16 bars. **No Windows runtime build or Cubase fix verification on this revision: unresolved.** Audit: [docs/FULL_SOURCE_AUDIT_2026_10_18.md](docs/FULL_SOURCE_AUDIT_2026_10_18.md).

### New critical diagnosis: live MIDI vs sequenced MIDI (2026-10-18)

User reports master-keyboard VSTi live notes trigger normally, whereas programmed/drawn MIDI note playback in Cubase 8 can generate later-bar music when the transport starts over an empty section at bar 0. This is a sequencer playback / real-time VST path differential, **not proof of buffering latency or a direct MIDI bug in the ASIO driver**. Monitored/record-enabled tracks and normal playback tracks can take different real-time/ASIO-Guard processing routes. Next test: toggle **only Record Enable on the same VSTi track** and play the same MIDI part without recording; see [docs/CUBASE_LIVE_VS_SEQUENCED_MIDI.md](docs/CUBASE_LIVE_VS_SEQUENCED_MIDI.md). No new source patch or cure claim.

### ASIO double-buffer output handoff correction (2026-10-18, Windows test pending)

Official ASIO SDK `bufferSwitch` and reference FlexASIO require the OUTPUT hardware to consume the **opposite** half of the ASIO half Cubase starts writing. This driver mistakenly queued the just-written same half after callback. Changed to opposite half and removed duplicated startup B enqueue; added distinct-output-half, 100-callback FIFO regression. User's constant ~24-bar future-audio and silent sequenced MIDI are **not yet verified fixed** and may have another cause. Documentation: [ASIO_OUTPUT_HALF_HANDOFF](docs/ASIO_OUTPUT_HALF_HANDOFF.md). Original Steinberg DLL and registration unaffected.

### Cubase 8.0.40 playback recovery — user-confirmed (2026-10-18)

After correcting ASIO's alternating buffer handoff (the host fills the callback half while the device outputs the opposite half), plus eliminating redundant initial B enqueue, the user reported that **all playback is working**. This follows reproducible problems with fixed ~24-bar forward Audio-track source output and silent programmed MIDI/VST, while live master-keyboard notes played. This is a meaningful **on-user-machine functional confirmation**, not proof of general production readiness; comprehensive Windows test logs, multidevice testing, durability and measured latency remain outstanding. See [ASIO output handoff](docs/ASIO_OUTPUT_HALF_HANDOFF.md). Original Steinberg DLL not modified.

### Performance optimization phase 1 — verified baseline preserved (2026-10-18)

Working Cubase 8.0.40 source is protected on GitHub branch `stable/cubase8-verified-20261018`. Enabled an output-preserving **direct planar-to-FIFO** copy (one atomic SPSC publication instead of four temporary-buffer writes) and added a separate, **opt-in** experimental one-block FIFO target (`HKCU\Software\ModernSharedASIO\QueueTargetBlocks=1`; default remains 2). In local Clang -O3 microbenchmarks the direct copy is 1.44x faster on stereo and 1.36x on mono, but this does **not** imply equal Cubase latency or CPU gains. The lower queue target saved 512 frames in an idealized simulation; Windows glitch safety not yet measured. Total expected Windows tests: **16**. See [Optimization Phase 1](docs/OPTIMIZATION_PHASE1.md).

### Optimization phase 2: direct native float32 WASAPI output (Windows verification pending)

For a native Windows shared-mode **stereo Float32** endpoint and asynchronous ASIO source, FIFO PCM now writes directly into `IAudioRenderClient::GetBuffer` output memory, removing an intermediate scratch-buffer copy; all other formats retain the previous conversion path. The experimental driver logs `native_direct_frames` to confirm that optimization activated. Local Clang 17 ASan/UBSan differential 60k cases PASS and a 1056-frame microbenchmark showed 0.3095->0.2173 us/call (~1.425x for the narrow transfer only). **Not** a proven Cubase-wide or latency improvement. Windows total expected CTests: 17, still to be checked; original double-buffer handoff and 512-frame default remain untouched. See [Phase 2](docs/OPTIMIZATION_PHASE2.md).
