# Phase 1 Optimization — preserve Cubase 8.0.40 working baseline

Date: 2026-10-18 (KST)
User-observed good commit: `d3d770b308b7055763da1d9c04900ed60d7e243e`.
Documentation-only follow-up: `19f79a92486d30ffa463296fa0eb078cb928229d`.
Safety branch: `stable/cubase8-verified-20261018` points at `19f79a9`.

**Safety:** Keep the ASIO host callback half vs hardware output half correct (A->B, B->A). Never rewrite or register over Steinberg's original ASIO driver. Default ASIO block remains 512 frames, queue target remains 1024 frames, TimeInfo remains OFF unless opt-in, and current 5ms requested WASAPI period is unchanged. CI is deferred until final.

## Performance investigation — measured and not measured

| Component / experiment | Baseline | Candidate | Evidence | Deployment |
| --- | --- | --- | --- | --- |
| ASIO Float32 planar to stereo FIFO | 512-frame buffer processed as four 128-frame stack scratch interleaves and four SPSC writes | direct planar interleave to ring slots and one release-store publication | Clang 17 -O3 Linux 512-frame microbenchmark: **stereo 0.1918us -> 0.1333us, 1.44x**; **one-channel 0.1583us -> 0.1162us, 1.36x** | Enabled by default, PCM-equivalent in local differential tests |
| Safe default producer target | 2 x 512=1024 frames | unchanged | known user's Cubase working configuration, no evidence of interruption | default |
| Optional lower queue target | 1024 frames | 512 frames via HKCU `QueueTargetBlocks=1` | deterministic 200k-event @48k simulation for 480-frame periods: average queue **1264 -> 752 frames**, a **512-frame/10.67ms** difference. For 441 frame periods: 1279.5 -> 767.5 frames. | strictly optional; Windows+Cubase dropout and audible latency not yet tested |
| ASIO callback block size | 512 frames | candidate 256 frames | potential ~5.33ms less buffer period, but more host callback work, and ASIO host must renegotiate buffer size | **not implemented**, not approved without A/B validation |
| WASAPI period | requested 5ms, observed 480 frames=10ms at 48k | supported device-native lower period | lower period is NOT supported on observed machine per logs; do not invent device capability | unchanged |
| ASIO reported latency | pre-start 1024 frames, after renderer initialized observed 2080 frames | calibrate to true output path and stabilize reporting | existing reporting is provisional and should not be treated as a measured physical latency | **not changed** |

**Important:** A 1.44x improvement in an ~0.2-microsecond data transfer subroutine is **not** a 1.44x improvement in Cubase CPU or audio latency. No end-to-end hardware performance improvement is yet established.

## Code

1. `SpscStereoRing::push_planar(left,right,frames)` writes directly into the preallocated SPSC queue. Null channels and wrap-around are handled; publishing the write index **once after all samples are written** prevents a consumer from seeing partially written data. The previous interleaved `push()` and `pop()` APIs are preserved.
2. `AsioPlaybackBridge::submit()` calls `push_planar` once, preserving accepted/dropped count and stereo sample identity.
3. `tests/planar_fifo_equivalence_tests.cpp`: identical outputs against a copy of the old algorithm for **120,000 randomized operations** spanning 8 capacities (including overflow and wrap) and **250,000 frames** in a simultaneous SPSC producer/consumer.
4. `tools/bridge_bench.cpp`: opt-in microbenchmark. Build with `-DMSA_BUILD_BENCHMARKS=ON`, do **not** run benchmarks in the final CI or as ordinary CTests. Use Release builds and repeated trials.
5. `HKCU\Software\ModernSharedASIO\QueueTargetBlocks` DWORD: missing/invalid/2 -> 2 blocks (known safe baseline); 1 -> 1 block (experimental lower queueing). Read at `start()` before audio worker. Stop log's `audio_buffer_target_frames` reports the configured target. **Requires closing/restarting Cubase to reload the driver setting**; registry settings alone do not validate actual latency.
6. `tests/queue_target_tests.cpp`: 200k deterministic render events per combination of 441/480 frame periods and 512/1024-frame target. Verifies no drop in the idealized model and approximately 512 fewer average queued frames. **Not** a scheduling jitter/stress proof.

## Reproduce on the user's Windows machine

**Baseline:** Keep `QueueTargetBlocks` absent for the first test. Close Cubase, rebuild using:

```powershell
cd C:\ModernSharedASIO
git pull --ff-only
.\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug
```

Expect **16 CTests** total (15 portable core + 1 COM preflight, if BUILD_TESTING=ON and MSA_BUILD_ASIO_DLL=ON). The registered DLL path remains the same; don't re-register if existing COM path is correct.

Use a duplicate of the previously verified Cubase project. Verify Audio bar position, drawn MIDI/VST playback, live keyboard and multi-track load. Log after stop:

```powershell
Get-Content "$env:TEMP\ModernSharedASIO-debug.log" -Tail 2
```

**Experimental lower-latency A/B (only after baseline passes):** close Cubase and set:

```powershell
New-Item -Path 'HKCU:\Software\ModernSharedASIO' -Force | Out-Null
New-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name QueueTargetBlocks -PropertyType DWord -Value 1 -Force | Out-Null
```

Restart Cubase, repeat same real project and long VST run. Compare `underrun_frames`, `callback_errors`, `max_host_callback_us`, audio clicks, audible latency, and CPU. To restore safe two-block default, close Cubase:

```powershell
Remove-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name QueueTargetBlocks -ErrorAction SilentlyContinue
```

**Do not** change `PeriodMs`, `EnableTimeInfo`, ASIO-Guard, ASIO buffer size or sample rate simultaneously: that makes A/B attribution impossible.

## Verification

- New planar direct-write and queue-target C++ test behaviors were mirrored locally and compiled on Linux with **Clang 17 AddressSanitizer + UndefinedBehaviorSanitizer; all 120k randomized queue operations, 250k concurrent frames, and 200k events in four queue-target scenarios passed**.
- Local Release bridge benchmark repeated six runs per channel mode and gave the throughput figures above.
- **Windows MSVC compilation, 16/16 CTests, real Cubase, and latency/underrun outcomes on this optimization commit remain unverified.** Preserve the stable branch until all are confirmed.

## Next highest-payoff steps, after baseline and optional A/B

1. Measure actual DPC/ISR and process scheduling jitter; profile real Cubase CPU in Windows Performance Recorder/Analyzer rather than guessing.
2. Experiment separately with an optional 256-frame ASIO callback configuration with host negotiation and all existing handoff/timeline tests extended. Keep 512 default.
3. Consider a native Float32-stereo WASAPI buffer-fill path that avoids the renderer's scratch/intermediate memcpy, if sample-format/ownership checks prove safe.
4. Improve `getLatencies()` to report stable and physically meaningful values, verified with measurement rather than subtracting made-up milliseconds.
5. Test sample-rate/device migration, long-run stability, heavy VST/ASIO-Guard, and restart cycles before shipping.

## Phase 2 continuation

The next stage replaces FIFO->scratch->WASAPI memcpy with direct FIFO->device writing for native asynchronous Float32 stereo only. All Cubase ASIO 512-frame/output half semantics remain fixed. See [Phase 2](OPTIMIZATION_PHASE2.md) and the new 17th CTest. Real Windows device results remain pending.
