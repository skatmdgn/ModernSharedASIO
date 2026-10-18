# Windows MSVC build/debug handoff (2026-10-18)

- GitHub main base `84e7f78c` before these fixes.
- User Windows compile: Visual Studio 2022 MSVC 19.44.35228.0, Windows SDK 10.0.26100.0; configure and WASAPI backend + smoke passed.
- Blocker: errors `C2375 DllGetClassObject` and `C2375 DllCanUnloadNow` (combaseapi.h redeclares entrypoints via STDAPI).
- Fix: matching `STDAPI` signatures in driver source and explicit MSVC `.def` for four COM exports; C4005 macro guards.
- Preflight verifies all export addresses but **does not invoke DllRegisterServer or DllUnregisterServer**.
- Next user command after `git pull`: `./scripts/build_windows_x64.ps1 -AsioSdkDir C:/sdk/asio -Configuration Debug` (in PowerShell; if execution policy requires, use `powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build_windows_x64.ps1 -AsioSdkDir C:\\sdk\\asio -Configuration Debug`).
- Expected: MSVC build then CTest (8 portable tests + 1 registry-free COM preflight). Windows compilation and tests **not yet verified** after this fix.
- Never overwrite original Steinberg `asioglld.dll`; do not register until controlled validation.

## Resolved: MSVC Debug build and COM factory preflight passed (2026-10-18)

**Evidence: user-provided complete local Windows PowerShell log**, repository HEAD `4a6a68eb` (fast-forward confirmed by git pull and git log).

- CMake configure/generate succeeded; MSVC `19.44.35228.0`, Windows SDK `10.0.26100.0`, target Windows `10.0.22631`.
- `ModernSharedASIO.vcxproj -> C:\ModernSharedASIO\build-win\Debug\ModernSharedASIO.dll` succeeded.
- WASAPI static backend, `modern_asio_smoke.exe`, `modern_asio_preflight.exe`, and all eight portable test executables built.
- `ctest --test-dir build-win -C Debug --output-on-failure`: **9/9 PASSED**, including COM factory preflight, total reported runtime 0.51 seconds.
- **No C2375 errors** after matching the SDK's STDAPI declarations.
- Remaining: four **nonfatal** linker warnings `LNK4104` for COM DLL exports; these may be addressed later with `PRIVATE` in the .def file but do not prevent GetProcAddress or the successful COM preflight.
- Tests only exercise portable code and registration-free COM creation; the user has **not yet run actual WASAPI playback**, installed the driver, opened Cubase with it, or compared simultaneous YouTube playback.

**Immediate next test (do not register DLL):** `cd C:\ModernSharedASIO; .\build-win\Debug\modern_asio_smoke.exe --tone` with Windows output sound at a moderate volume. Then retry while YouTube is playing, and capture output stats/HRESULT/clock. The 440 Hz test tone is approximately -36 dBFS for five seconds. `underrun_frames` may reveal initial scheduling issues and is not automatically a CTest failure. Do not assert audio functionality until tested by user.

**Phase gate:** `BUILD_VERIFIED=true (Debug x64)`, `COM_FACTORY_PREFLIGHT=true`, `WASAPI_RUNTIME_VERIFIED=false`, `CUBASE_VERIFIED=false`, `PERFORMANCE_COMPARISON=false`. Release x64 is not yet built.

## User-verified standalone shared playback and YouTube coexistence (2026-10-18)

User reports that the 440 Hz test tone and YouTube audio **were audible at the same time**. The following console telemetry was supplied, not simulated:

```
SHARED ONLY: rate=48000 Hz, channels=2, buffer=1056 frames, engine_period=480, IAudioClient3=1, reported_latency_100ns=0
Quiet 440Hz tone for 5 seconds
render_events=500 underrun_frames=960 stream_errors=0 producer_failures=0
last_stream_hresult=0x0
submitted_frames=241056
coherent_played_frames=236681 paired_qpc_ns=1770048398745700 approximate_submitted_minus_played=4375
clock_reads=62 device_ticks=1893448 frequency=384000 qpc_100ns=17700483987457
```

### Interpretation and caveats

- **WASAPI_RUNTIME_VERIFIED (basic playback+YouTube)=true**, independent of ASIO registration. Directly observed: user heard both simultaneously; IAudioClient3 selected; 5 seconds / 500 event signals; no render stream HRESULT failures.
- At 48kHz, 1056 output frames correspond to 22ms buffer capacity; event period 480 corresponds to 10ms.
- 960 underrun frames = 20ms of aggregate missing audio, from standalone app's 10ms `sleep_until`-paced producer. **Not proof of Cubase callback underrun**, and actual timestamp of shortages was not captured.
- `GetStreamLatency` field printed as zero. This does not establish zero end-to-end playback latency; API result and hardware path remain unmeasured.
- The clock observation is only updated once every 8 render events (62 reads for 500 events). Its `coherent_played_frames` value is necessarily sampled earlier than final `submitted_frames`. The displayed difference of 4375 frames (91.15ms) is **not a calibrated latency measurement**. It also includes consciously prefilled startup silence.
- `device_ticks=1893448` and `frequency=384000` represent 236681 frames at 48kHz as calculated by the exact integer conversion. This confirms the diagnostic calculation, not acoustic latency.
- Current risk: ASIO host runtime remains **UNVERIFIED**; do not register/install DLL until a separate ASIO-specific playback path has been checked.

### Next safe test: registry-free ASIO playback

- A new executable `modern_asio_host_smoke.exe` loads the experimental DLL **by file path only**, constructs IASIO via DllGetClassObject, calls IASIO::init, queries stereo buffers and sample rate, creates 512-frame stereo buffers, starts the driver, and feeds them via a host-supplied ASIO callback for 5 seconds.
- Both legacy `bufferSwitch` and time-info `bufferSwitchTimeInfo` callback modes are exposed; the latter is opt-in. Tone amplitude is ~-40dBFS. No registration/registry calls.
- After user runs `git pull --ff-only` and manual Windows build, test with: `build-win\\Debug\\modern_asio_host_smoke.exe build-win\\Debug\\ModernSharedASIO.dll --tone` from repository directory.
- Source currently awaiting user MSVC compilation and real playback. **Do not declare this test passed or Cubase compatibility based on the existing standalone WASAPI test.**


## Verified registry-free ASIO host real-device audio (2026-10-18)

The user reports that the 440Hz ASIO tone and YouTube played concurrently. The actual Windows host smoke output was:

```text
Registry-free ASIO host: rate=48000, block=512, channels=2, mode=bufferSwitch
Quiet 440Hz ASIO tone for 5 seconds
ASIO callbacks=472 invalid_buffers=0 sample_position_low=242176
Registry-free ASIO host smoke: PASS
```

- **REGISTRY_FREE_ASIO_PLAYBACK_VERIFIED=true** for the tested legacy `bufferSwitch` mode at 48000Hz stereo, 512 frame blocks. This means IASIO init/createBuffers/start/host callbacks/stop/disposeBuffers passed and produced audible output.
- Callback counter consistency: `472*512+512=242176` exactly equals returned sample position. The extra 512 is the driver's ASIO B-half pre-roll. It is not a measurement of acoustic or physical device-clock latency.
- Host buffers validated: `invalid_buffers=0`. This manual host test did not measure actual xruns inside the ASIO renderer, audio latency, CPU load, or click/dropout statistics beyond user observation.
- **CUBASE_8_0_40_VERIFIED=false**, **ASIO_TIMEINFO_VERIFIED=false** and **STUDIO_PERFORMANCE_A_B_VERIFIED=false**. The driver is still not installed/registered.
- Next safe manual test: `modern_asio_host_smoke.exe <DLL> --tone --time-info` (no registry modifications); subsequently make a separate opt-in Cubase driver registration checklist with backup and reversal steps. The original Steinberg driver must not be modified.

## Verified ASIO TimeInfo playback and optional Cubase registration (2026-10-18)

User confirmed 440Hz ASIO output **simultaneous with YouTube** using `bufferSwitchTimeInfo`, with these exact Windows console results:

```
Registry-free ASIO host: rate=48000, block=512, channels=2, mode=bufferSwitchTimeInfo
Quiet 440Hz ASIO tone for 5 seconds
ASIO callbacks=471 invalid_buffers=0 sample_position_low=241664
Registry-free ASIO host smoke: PASS
```

Accounting: 471 * 512 + 512 startup preroll = 241664. Both legacy bufferSwitch and ASIO 2.x TimeInfo callback paths are now Windows-runtime VERIFIED using a separate minimal ASIO host. Cubase 8.0.40 itself is NOT YET VERIFIED; neither callback passing proves host stability, precise latency, or CPU improvement.

Two opt-in Windows PowerShell scripts added as **source-written, not yet executed**:

- `scripts/register_cubase_test.ps1` refuses existing registry name/CLSID conflicts, demands elevated 64-bit PowerShell, confirms Cubase is closed, runs registry-free preflight, backs up the existing HKLM SOFTWARE ASIO list under the ignored `build-win/registry-backup` folder, registers only our independent Modern Shared Audio CLSID via x64 regsvr32, and verifies the registered path and CLSID.
- `scripts/unregister_cubase_test.ps1` verifies that the experimental registry keys still refer to *this* CLSID and DLL, then invokes 64-bit regsvr32 /u. It refuses mismatched entries to prevent deleting others.

Cubase initial gate: close Cubase, use a 64-bit PowerShell **Run as Administrator**, `git pull --ff-only`, run `scripts/register_cubase_test.ps1 -Configuration Debug`. Launch Cubase 8.0.40, choose Devices > Device Setup > VST Audio System > ASIO Driver > Modern Shared Audio. Create an EMPTY project at 48 kHz, configure Devices > VST Connections > Outputs > Stereo Out to ASIO channels 1-2 if needed, then play a built-in test tone/audio and YouTube at the same time. Report whether the driver is listed and loads, any messages or crashes, audio quality or dropouts. Do not use a valuable production project. To revert, close Cubase and run `scripts/unregister_cubase_test.ps1 -Configuration Debug` in elevated PowerShell. The original Steinberg ASIO driver remains separate.

**Registration has not been performed by the assistant or user in this conversation at this stage.** CI stays deferred to final release gate.

## Cubase real plugin regression / diagnostic source written

User reports audible audio-file playback but severe delay, breaking VST instruments and silent Serum in Cubase 8.0.40. The miniature ASIO host only validated trivial 440Hz generation. We introduced 5ms shared-mode period request as a reversible performance test, plus stop-time ASIO callback and WASAPI underrun diagnostics. These are not yet Windows built or tested. See docs/CUBASE_DIAGNOSTICS.md. The previous successful WAV/YouTube and minimal ASIO tests remain valid, but Cubase production-quality gate fails.

## Cubase callback sample-position correction staged (2026-10-18)

User four-stop logs: requested 5ms, actual 10ms engine at 48k, some runs with 0 underrun despite broken VST, one or two over-budget host calls (max 18.6/14.7ms). Found confirmed ASIO 2.3 first-callback sample position violation: prior mini-host `sample_position_low = callback_count*512+512` is contrary to the spec; first callback position must be 0. Source now corrects sample position, adds host contract assertions and ASIO host output amplitude diagnostic counters. User Windows build and actual Cubase/Serum retest **NOT YET DONE**. See docs/ASIO_TIMELINE_FIX.md. Original driver unchanged.

## Multi-track Cubase wrong source position — outputReady implementation defect (2026-10-18)

Latest user report: single track normal; heavier track count produces increasing delay, stuttering/absent VSTi and apparently a ~16-bar future audio segment at bar 0. Source audit found `IASIO::outputReady()` incorrectly claiming optional support via ASE_OK without real async completion support, directly contradicting official ASIO 2.3 returns specification. Now ASE_NotPresent plus fail-fast preflight and mini-host contract checks. This is source-written only, not yet Windows compiled or Cubase-tested; 16 bars unverified as measured offset.

## Regression persists despite corrected outputReady (2026-10-18)

User reports NO improvement and supplies three stopped-session logs, all WASAPI underrun=0 and TimeInfo=1. Session one: 6417/6417 host blocks silent; second: 5341 callbacks, 3488 silent, 1853 nonzero; third: 5 silent blocks. Zero backend errors and outputReady contract fix therefore do not establish functional correctness. A stopped session may include transport idle; aggregate silence stats cannot locate failure at bar 0. Requested 5ms effective 480-frame/10ms, output latency last reported 2080 frames. No further untested source modifications: isolate Cubase ASIO-Guard and TimeInfo with one-at-a-time A/B (docs/CUBASE_CAUSAL_DIAGNOSIS.md).

## Future-bar audio with silent timeline origin (2026-10-18)

User clarifies that playhead=0 covers an EMPTY project section, yet audio from approx bar 16 ahead is heard; track count amplifies discrepancy. This is forward content substitution, not mere latency. With prior WASAPI 0 underruns and a bounded 1024-frame FIFO target, internal queue is not obviously able to hold 16 bars of 48k PCM. We have NOT proved the source of later music. To avoid relying on uncalibrated ASIOTimeInfo, default callback mode changed to legacy bufferSwitch, a new distinct 16-bar empty-content PCM marker ordering test and live callback/wall-time metrics added. Only modeled PCM policy/order in independent JS (first marker at 1536512), no Windows build or Cubase test. See docs/CUBASE_FUTURE_BAR_FIX.md.

## Full source audit (2026-10-18)

ModernSharedASIO active code audited against Cubase empty bar0/future bar16 audio. Queue bound only 32768 frames (<0.7s) and 1024-frame target, so the current queue alone cannot buffer 16 bars. Key confirmed SDK time contract violation: getSamplePosition had block-aligned sample position but timestamp sampled freshly via QPC (and TimeInfo Windows clock source QPC vs required timeGetTime). Added coherent ASIO block stamp channel, SDK-domain timeGetTime, mini-host consistency asserts, and planar buffer-zero before callback to eliminate stale output on skipped writes. Added 2 portable tests and local Clang ASan/UBSan PASS. MSVC Windows compile / Cubase test NOT RUN for this source revision. See docs/FULL_SOURCE_AUDIT_2026_10_18.md.

## New decisive Cubase observation: live VST input works, programmed MIDI playback does not

2026-10-18 user: playing via MIDI master keyboard triggers VST notes at correct time, while playing placed/drawn MIDI parts produces wrong time/content including source from around 16 bars later at empty playhead=0. This is distinct from output buffering latency. The mini-host never tested MIDI scheduling. Steinberg describes real-time monitored/record-armed VST paths vs ASIO-Guard pre-rendered unarmed paths; actual cause still open, earlier ASIO-Guard change not reported as a fix. See docs/CUBASE_LIVE_VS_SEQUENCED_MIDI.md.

## Callback output half violation found, source correction (2026-10-18)

Constant audio output of material ~24 bars later at Cubase bar0 contradicts earlier engine-idle drift hypothesis. The ASIO SDK bufferSwitch documentation and FlexASIO's working copy-to-device implementation require outputting the OTHER half, not same just-written `bufferSwitch` half. In ModernSharedASIO the code did the latter; fixed to opposite-half FIFO submit, removed eager B start enqueue (avoid double B), added 100-step distinct A/B host output regression. **Only source written; Windows 14 CTests and Cubase still pending.**

## 2026-10-18 — user reports Cubase playback restored

After the commit fixing ASIO alternating output-half semantics (d3d770b), the user confirmed “됐어! 이제 모든게 잘나와”. Previously observed fresh-project Audio bar-0 output of ~24-bars-ahead content, no programmed MIDI/VST note playback and normal live keyboard performance were the blocking context. Record a **user-observed playback PASS**, not an independently available CTest transcript or exhaustive hardware compatibility certification. Whether all 14 tests passed on this precise commit was not pasted into chat. Keep original driver registration unmodified.

## Phase 1 output-preserving optimization (2026-10-18)

No Win32 real-device builds or Cubase retests have been run yet for this commit. Added direct planar SPSC FIFO write (one publication instead of 4 scratch+memcpy publishes), opt-in `QueueTargetBlocks=1` (default 2, untouched), randomized equivalence and queue simulation CTests. Clang 17 Linux ASan/UBSan differential 120k, concurrent 250k frames PASS; deterministic 200k render events x4 scenarios PASS. Clang 17 -O3 microbench stereo 0.1918->0.1333 us/block, mono 0.1583->0.1162; these are not Cubase whole-system metrics. Stable Cubase-tested branch `stable/cubase8-verified-20261018`; expected Windows CTests 16. docs/OPTIMIZATION_PHASE1.md.

## Phase 2 WASAPI direct render — local validation only (2026-10-18)

Changed asynchronous native stereo Float32 renderer to FIFO.pop directly into IAudioRenderClient::GetBuffer output; all other formats still stage/convert through scratch. New native_direct_frames counter. Linux Clang 17 ASan+UBSan native differential: 60,000 random cases PASS; capacity-overflow device chunk PASS; prior SPSC and pacing tests rerun PASS. -O3 1056-frame microbenchmark staged 0.3095 us/call, direct 0.2173 us/call (1.425x memory-path). Do not extrapolate to Cubase latency/CPU. MSVC Windows test (17 CTests) and Cubase user acceptance pending. See docs/OPTIMIZATION_PHASE2.md.

## Experimental ASIO 256-frame branch — local-only status (2026-10-18)

Branch `experiment/asio-256-frames` from main phase2. Implemented opt-in DWORD AsioBlockFrames=256 (default 512). Propagated selected size consistently through host negotiation and producer timing while preserving reverse half-handoff. Local Clang 17 ASan+UBSan: 5000 callback blocks per mode and 200000 device events each combination (256/512 x 441/480) PASS. MSVC Windows build and 18 CTests NOT YET RUN, Cubase MIDI/VST and real latency/drops unknown. docs/EXPERIMENT_256_ASIO.md.
