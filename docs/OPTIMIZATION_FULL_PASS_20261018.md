# Whole-driver optimization audit and full-pass experiment

Date: 2026-10-18. Branch: `optimization/full-pass-20261018`, based on `main` SHA `3a4c4cd974f0b9fdf1747953ac4a20238cea06a2`. **This branch is not merged into main until the Windows/Cubase acceptance gate passes.**

## Strict compatibility invariants

- ASIO 512-frame callback and 48kHz user-default configuration remain unchanged. No host bufferSize contract change, no new outputReady capability.
- ASIO `bufferSwitch(index)` host-writes `index`, while the *opposite* half is sent to the hardware. This fixed real Cubase playback and is NEVER altered by optimization.
- FIFO target absent/2 -> 1,024 frames; experimental 1-block mode remains opt-in and is not promoted. SPSC ring capacity remains 32,768 frames.
- Cubase TimeInfo disabled by default; IASIO callback sample positions, ring PCM, WASAPI direct native Float32 stream, device period and COM registration unchanged.
- No edit to original Steinberg `asioglld.dll` or its registration. New features are on the experimental GitHub branch, not the working main.

## Completed code optimizations

### 1. QPC frequency cached, callback duration uses ticks delta

Previously `qpc_ns()` queried `QueryPerformanceCounter` AND `QueryPerformanceFrequency` on every invocation, including two full nanosecond conversions per Cubase host callback. Windows QPC frequency is invariant during a boot. The new `qpc_frequency()` caches it in a function-local static initialized before producer start, while callback duration measures raw QPC begin/end ticks and performs exactly one ticks-to-microseconds conversion. Absolute wall-clock nanoseconds still use quotient/remainder math consistent with earlier code. The ASIO `timeGetTime()` timestamp behavior is unchanged.

New standalone `modern_asio_qpc_time_tests` checks multiple realistic frequencies and approximately 1us integer truncation difference versus the prior absolute-nanosecond approach.

### 2. Move producer-exclusive statistics from atomic RMWs into producer-owned state

The original producer did atomically updated callback counts, callback maximum/overbudget, silent/nonzero host block counts, invalid float counters and max L/R peaks. These metrics are never needed until after `stop_producer()` joins the worker. New `HostPcmTracker` holds the exact same classification and peak logic in non-atomic producer-owned state; `CallbackTimingTracker` additionally keeps `max_us` and existing `over_budget` so legacy ASIO_STOP names and meanings remain unchanged. All metrics are read only after the producer has joined. External/shared `producer_wakes`, `callback_errors`, and IASIO sample timestamp channel keep appropriate atomics.

New `modern_asio_host_pcm_stats_tests` compares old-vs-new per-block PCM statistics, including null channels, silent blocks, NaN, infinity, threshold and repeated peaks. A separate local Clang AddressSanitizer/UBSan + GCC Release comparison of **15,000 blocks x 512 frames** produced identical metrics. No audio samples are changed.

### 3. Skip no-op producer SetEvent notifications after WASAPI output

Previously the WASAPI worker always called `SetEvent(producer_wake_event_)` after successfully rendering a period, even when the FIFO still held its configured producer target. Producer then woke and computed **zero** ASIO blocks to fill. New `producer_needs_wakeup(queued,target)` signals only when queued < target, or when no target exists; it checks queue occupancy after consumption. When occupancy falls below target, the next render request wakes the producer normally. This does NOT skip any required Cubase host callback blocks under the producer fill policy.

Portable `modern_asio_wake_policy_tests` compares total produced callback blocks, final queued frames and underruns over 100,000 modeled events for output periods 441/480/512/960/1056 and queue targets 512/1024, old unconditional vs new conditional notifications. All final data matches. Under ideal steady-state 480-frame/48k conditions: **6.25% fewer OS wake requests**; at 441 frames: ~13.87%; at 512-frame output period: zero saving. **This is not a claim of 6.25% lower Cubase CPU or 6.25% lower latency.** Real Windows scheduling may differ and must be measured.

### 4. Remove one zero-timeout Windows wait call per produced callback block

Producer loop previously called `WaitForSingleObject(producer_stop_event_,0)` once per ASIO block to check for shutdown; new `producer_stop_requested_` atomic release/acquire flag replaces that kernel handle poll, while the existing stop event still interrupts `WaitForMultipleObjects` when producer is asleep. Reset occurs before the thread starts and stop sets the flag before joining. `modern_asio_atomic_stop_tests` covers a concurrent producer loop, stop, join and reset. Full Win32 teardown races still need Windows stress.

### 5. Non-invasive Release benchmark workflow

New `scripts/benchmark_core_windows_x64.ps1` uses an isolated `build-benchmark` directory to compile core/WASAPI benchmark programs in x64 MSVC **Release** mode with no ASIO DLL build or registration. It runs CTests unless `-SkipTests` is given, then compares old/new copy strategies already implemented by `modern_asio_bridge_bench` and `modern_asio_native_float32_bench`. The user's registered Debug DLL and original Steinberg DLL are untouched. Those microbenchmarks measure internal copies, not Cubase host CPU or physical latency.

## Audited and deliberately NOT changed

- SPSC ring memory capacity: local optimized GCC benchmark compared 4096 vs 32768 frames over 3.5M modeled render events per trial. Across alternating trials, latency fluctuated roughly 94-112ns/event with **no repeatable advantage** for either ring size. Maintain the already-working 32768-frame ring; saving 224KiB of allocated memory alone does not warrant raising compatibility risk.
- 256-frame ASIO buffer: doubles host DSP callback rate and makes headroom shorter; previous user confirmed 512/1024 stable. Keep `experiment/asio-256-frames` separate.
- `EarlyProducerWake=1`: separate opt-in experiment with mixed real results; one ON music run had 480 total missing frames at later WASAPI events; no controlled improvement established. Do not silently merge.
- WASAPI device period: actual 480-frame period at 48k despite requesting 5ms. Do not invent a lower period without device support.
- ASIO `getLatencies`: observed 1024 pre-init versus 2080 post-init provisional frames; must not 'optimize' by lying to Cubase. Need independent measured output-latency/host compensation design.
- ASIO PCM scan for silence, peaks and invalid floats: retained because it supplies useful, faithful playback provenance; only per-callback atomic bookkeeping changed.
- PCM format conversion loop, MMCSS class/priority and device graph: no guessed gains or unsafe host scheduling changes without profiling evidence.

## Actual test evidence

- Local standalone logic reproductions compiled/executed under Clang 17 C++20 AddressSanitizer + UndefinedBehaviorSanitizer **PASS**: host PCM 15,000 x 512 blocks equivalence; QPC four frequency families; wake predicate all 10 model combinations; concurrent producer atomic shutdown test.
- The same tests also passed Linux GCC `-O3 -DNDEBUG` in this working environment. Newly committed CMake tests use an explicit `CHECK` abort macro that remains active in Release, rather than `assert` elided by NDEBUG.
- New additions: **4 CTests** (QPC timing, PCM stats, producer wake policy, atomic stop); total Windows expected **23 tests** (22 portable + COM preflight), if ASIO SDK DLL configuration is enabled. No CI triggered per project policy.
- **NOT VERIFIED:** compilation of all modified Windows COM driver source under MSVC, 23 Windows CTests, Cubase playback, realtime CPU savings, output latency, callback scheduling under heavy VSTs, repeated start/stop and device migration. No claims of production readiness or end-to-end performance improvements.

## Manual Windows gate

Do not replace the known-good registered driver until an x64 MSVC build succeeds. Close Cubase and preserve local uncommitted files before switching branches:

```powershell
cd C:\ModernSharedASIO
git fetch origin
git switch optimization/full-pass-20261018
git pull --ff-only
.\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug
```

On failure return to `stable/cubase8-phase2-1024-confirmed` and rebuild. On success, with `QueueTargetBlocks` absent and TimeInfo disabled, run the same VST+Audio+sequenced MIDI project for >=90 seconds with ASIO 512/FIFO 1024. Require correct transport timeline, live MIDI, browser simultaneous audio, ASIO opposite-half integrity, and compare `underrun_frames`, `underrun_events`, `callback_errors`, callback histogram, `producer_wakes`, and `native_direct_frames` before/after. Repeated stop/start cycles and high load are required.

Optional separate microbench, no registry/ASIO SDK:

```powershell
.\scripts\benchmark_core_windows_x64.ps1
```

Do NOT extrapolate benchmark copy-only ratios to whole-driver or DAW CPU; the user has already observed occasional host-callback spikes up to 23ms, far larger than the sub-microsecond bridge/WASAPI copy cost.

## Next pending engineering work

1. Win32 host callback system tracing with Windows Performance Recorder/Analyzer to attribute >10ms Cubase DSP spikes versus priority inversion or DPC stalls.
2. Device+format matrix, fault injection, long soak (>=30min) and secure shutdown on device changes.
3. Output latency measurement with calibrated wired physical loopback / ASIO and WASAPI endpoint timing. Only then stabilize `getLatencies` reporting.
4. Consider dedicated per-format SIMD conversion fast paths only if a real PCM16/24/32 or multichannel endpoint benchmark justifies complexity.
5. Promote changes to main only after real Windows acceptance, retain the working stable refs.
