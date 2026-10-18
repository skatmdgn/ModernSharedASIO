# ModernSharedASIO roadmap / session handoff (2026-10-18)

**Purpose:** Windows x64 shared-mode audio output for Cubase 8.0.40 with YouTube simultaneous playback. **Existing Steinberg DLL must never be overwritten.**

## Actual state

- [x] M0 — repository initialized, architecture, evidence crosswalk and core FIFO/period-policy source.
- [x] M1 source — IAudioClient3/shared-only WASAPI renderer, MMCSS, output diagnostic CLI, error/underrun counters.
- [x] M2 source — experimental 24-method IASIO interface, independent COM class factory and CLSID, exports/registration code, registry-free COM preflight program.
- [x] M3 portable source — ASIO Float32 planar→interleaved stereo bridge, preallocated FIFO, buffer wrap/overflow tests.
- [x] M3 prototype — ASIO callbacks and double-buffer producer attached to WASAPI render worker; no independent periodic software timer.
- [x] M1–M6 Windows **Debug x64 compilation + registry-free COM factory preflight** — user MSVC 19.44 / Windows SDK 10.0.26100 log confirms 9/9 CTest passed, HEAD `4a6a68eb`.
- [x] Real Windows WASAPI shared playback **alongside YouTube** — audible stereo 440Hz test, 48kHz IAudioClient3, 500 events, zero stream errors (user console). 960 underrun frames merit further diagnosis.
- [x] Registry-free IASIO host playback **VERIFIED on user's Windows machine**: legacy `bufferSwitch` 472 callbacks, 0 invalid buffers, valid `sample_position_low=242176`; 440Hz output and YouTube audible simultaneously.
- [x] ASIO `bufferSwitchTimeInfo` actual playback verified on Windows with YouTube: 471 callbacks, 0 invalid_buffers, 241664 frames (incl. 512 preroll).
- [x] Cubase 8.0.40 accepts independent ASIO driver and can play WAV tracks (user observed). [ ] Real Cubase audio/VSTi quality: FAILED with high latency and missing/stuttering Serum output.
- [ ] Windows Release x64 build NOT TESTED. Cubase 8.0.40 recognizes driver and outputs WAV tracks, but real-VST usability FAILED; stable YouTube coexistence during heavy VST not confirmed.
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
- Windows Debug x64 compilation: **VERIFIED on user's PC** (DLL and 9/9 CTest, including registry-free COM preflight). Release build: NOT DONE.
- WASAPI 440Hz plus YouTube: USER VERIFIED. Cubase 8 ASIO WAV playback: USER VERIFIED with excessive latency. Cubase VSTi/Serum quality: FAILED.
- No real-world audio improvement can be claimed yet. Source-written does NOT mean deployable.

## Windows build (Debug x64 successful; next step: actual playback smoke)

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

### First Windows user build: 2026-10-18

Environment: Visual Studio 2022 MSVC 19.44.35228.0, Windows SDK 10.0.26100.0, Windows 11 10.0.22631. CMake configure/generate PASSED; WASAPI static library, `modern_asio_smoke.exe` and eight portable test executables compiled. The ASIO DLL failed with `C2375` for `DllGetClassObject` and `DllCanUnloadNow` because the SDK declares STDAPI linkage; warnings C4005 occurred for repeated WIN32_LEAN_AND_MEAN/NOMINMAX macros. This commit changes exports to matching STDAPI definitions, makes export names explicit via `src/ModernSharedASIO.def` under MSVC linker, and guards source macros. It also checks four exports in registry-free preflight. **Follow-up 2026-10-18: user rebuilt after fix; Debug DLL built, 9/9 CTest including COM factory preflight passed.** Four nonfatal LNK4104 warnings persist; see docs/WINDOWS_BUILD_LOG.md. No regsvr32, no actual WASAPI playback yet, no Cubase test, and no CI.

### Registry-free ASIO host validation (manual only)

A Windows-only, manual ASIO host smoke target now compiles alongside the DLL, without automatic CTest execution or registration. Start with a quiet five-second stereo tone from the repository directory:

```powershell
git pull --ff-only
.\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug
.\build-win\Debug\modern_asio_host_smoke.exe .\build-win\Debug\ModernSharedASIO.dll --tone
```

Then optionally repeat with `--tone --time-info` to test ASIO 2.x bufferSwitchTimeInfo callbacks. Neither run writes Windows ASIO registry entries. This validates ASIO engine output directly before opting into any Cubase registration. **Follow-up: both legacy bufferSwitch (472 callbacks) and TimeInfo (471 callbacks) passed user Windows playback alongside YouTube. Cubase host registration/loading remains pending.** See docs/WINDOWS_BUILD_LOG.md.

### User-guided Cubase 8.0.40 opt-in registration

This is an intentional **Windows registry mutation** and must be run by the user only after closing Cubase and reviewing the script. The code refuses to replace existing ASIO entries and preserves the old driver. Both scripts are not yet tested on Windows.

```powershell
# Open 64-bit PowerShell as Administrator; close Cubase first.
cd C:\ModernSharedASIO
git pull --ff-only
.\scripts\register_cubase_test.ps1 -Configuration Debug
```

Then Cubase 8.0.40: Devices > Device Setup > VST Audio System > ASIO Driver > Modern Shared Audio; in an empty project set 48kHz to match the Windows mixer. If necessary, Devices > VST Connections > Outputs > Stereo Out output map L/R to ASIO 1/2. Test audio and YouTube concurrently. If there is any unexpected issue, close Cubase and rollback:

```powershell
.\scripts\unregister_cubase_test.ps1 -Configuration Debug
```

Registration is separate from replacing any existing Steinberg DLL. Backups under `build-win/registry-backup` are for inspection only; do NOT blindly re-import because that would overwrite later unrelated changes.

### Cubase live-VST regression found 2026-10-18

Cubase 8.0.40 loads the driver and plays audio waveform tracks, but heavy latency, VSTi dropouts and Serum silence occur. **CUBASE_PLAYBACK_QUALITY=FAILED.** Implemented a *diagnostic test candidate* in src/asio_com_driver.cpp: request 5ms WASAPI shared period (reversible per-user PeriodMs registry read), count host DSP callback wall times and backend underruns, append stop-time diagnostics. No heavy DSP or unnecessary features added. **New code not yet Windows compiled or retested.** See docs/CUBASE_DIAGNOSTICS.md. Do not claim resolved.

### ASIO callback position contract root bug (2026-10-18)

Found real ASIO SDK violation: first `bufferSwitch` position must be **0**, yet prior modern driver returned **512** because a prefilled output B buffer was counted as an ASIO callback. Added `AsioCallbackTimeline` source, 100,000-block portable regression, and strengthened mini-host checking every callback position (including TimeInfo), first position 0 and alternation. Locally mirrored portable test Clang ASan/UBSan and optimized Release passed. Windows CTest now expects 10 tests, yet **new Windows build and Cubase retest pending**. Sample peaks in the Cubase callback are now logged so the next Serum failure can be localized before/after FIFO; reversible `DisableTimeInfo=1` is available for A/B test. Crucially, the user's 5ms request produced **actual 480-frame/10ms period**, and zero WASAPI underruns on the long broken VST sessions, so changing buffer policy is not yet justified. See docs/ASIO_TIMELINE_FIX.md.

### Cubase multi-track content displaced ~16 bars (2026-10-18)

**New user evidence:** one track okay; more tracks trigger increasing delay/VST glitch, starting at bar 0 may play material ~16 bars later. This cannot be accounted for by the ~21ms software FIFO alone. Read ASIO SDK `ASIOOutputReady` return contract and found a confirmed driver violation: `outputReady()` unconditionally returned ASE_OK although no corresponding async-ready protocol exists; Cubase may therefore think it can hand off ASIO buffers asynchronously while the driver copied them immediately. Now return **ASE_NotPresent** and added COM preflight/mini-host assertions. **Source changed, not yet rebuilt or Cubase-verified; cause of 16-bar slip remains unproven.** See docs/CUBASE_MULTITRACK_OUTPUTREADY.md. If issue persists, A/B Cubase ASIO-Guard OFF and existing HKCU DisableTimeInfo=1 separately, then collect host_silent_blocks logs. No CI.

### Evidence isolation after user confirms outputReady did not fix Cubase (2026-10-18)

Three recent Cubase ASIO_STOP sessions: 6417/6417 silent host blocks; 3488/5341 silent with 1853 nonzero blocks; 5/5 silent. In all, WASAPI underruns, stream errors and producer failures zero; actual engine period 10ms despite 5ms request; TimeInfo negotiated in all. Crucial: these are **aggregate session metrics** (silence may be normal transport-stop idle), not bar-addressed transport evidence. The prior outputReady SDK fix did not improve the symptom. User-owned original ASIOGLLD notes describe device-clock/samplePosition timestamp correction absent from ModernSharedASIO, and Cubase ASIO-Guard/TimeInfo compatibility remains unisolated. **No more speculative latency patches before independent A/B**: (A) ASIO-Guard OFF with TimeInfo ON, (B) TimeInfo OFF via existing HKCU DisableTimeInfo=1 while ASIO-Guard back ON. See docs/CUBASE_CAUSAL_DIAGNOSIS.md. Driver NOT SUITABLE for serious sessions.

### User reiterates forward 16-bar musical *content* at empty transport start (2026-10-18)

This is NOT output-buffer latency: the audio source at bar 0 is empty but material located at around bar 16 plays. Previous hypotheses and ASIOOutputReady fixes did NOT resolve it. New isolation candidate defaults to legacy bufferSwitch rather than uncalibrated hardware TimeInfo (experimental TimeInfo opt-in via HKCU EnableTimeInfo DWORD=1). Added actual ASIO callback mode identification, 16-bar-silence+bar17-marker FIFO ordering regression and wall-vs-audio elapsed frame diagnostics. Independent JS policy simulation matched marker at exact expected output frame 1536512 with 0 underflow, but **C++ Windows build / Cubase outcome PENDING**. This test does not simulate Cubase ASIO-Guard. Read docs/CUBASE_FUTURE_BAR_FIX.md. Unsolved, not production ready.

### Full static source audit and next gate (2026-10-18)

Read executable ASIO driver, all WASAPI render code, FIFO/bridge, policy, callback test hosts, portable test sources, CMake/build and registration scripts. With user 0-bar future-content symptom, FIFO max 32768 frames (~0.683s) and regular ~1024-frame queue cannot hold 16 bars. Found and corrected still-broken ASIO getSamplePosition time pairing (QPC freshly sampled for same unchanged position, invalid per official SDK); moved to coherently latched samplePosition + Windows timeGetTime clock source, and zero the reused double-buffer half before Cubase callback to preclude stale PCM when host writes nothing. Added actual TimeInfo/getSamplePosition equality checks to host and two portable tests, **local Clang ASan/UBSan PASS**. Prior SDK fixes failed to resolve Cubase; no claims this one succeeds. Windows CTest count now 13, MSVC and real Cubase STILL PENDING. Open: unstable pre/post-start getLatencies, proper hardware clock calibration, project transport/source mapping and COM worker affinity. See docs/FULL_SOURCE_AUDIT_2026_10_18.md. CI deferred.

### Key real-time versus sequenced MIDI distinction (2026-10-18)

User confirms hardware master-keyboard performance on a VST instrument produces correct immediate sound, but MIDI notes written in the Cubase timeline play wrong material at bar0 (e.g. audio from ~16 bars later), increasingly with more tracks. Steinberg's Cubase 8-era ASIO-Guard docs explicitly distinguish monitored/record-enabled real-time path from unarmed sequenced pre-processing path. Earlier ASIO-Guard OFF reports were inconclusive/unchanged and must not be presented as a solved switch. **NO new speculative DLL fix:** A/B the very same MIDI part with only that track Record Enable ON vs OFF (without recording) before further source changes. See docs/CUBASE_LIVE_VS_SEQUENCED_MIDI.md.
