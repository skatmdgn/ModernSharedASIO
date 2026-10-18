# Experimental 256-frame ASIO callbacks (branch only)

Date: 2026-10-18. Branch: `experiment/asio-256-frames`. Base `074edfee9d062a8d50d33b80052f438f14f8b191`, which is phase-2 direct Float32 WASAPI output on main. Previously Cubase-verified stable source remains `stable/cubase8-verified-20261018`. **This experiment is NOT merged into main.**

## Why 256 and what not to infer

At 48kHz, 512 frames correspond to ~10.67ms per ASIO host callback, while 256 frames correspond to ~5.33ms. Reducing a 2-block producer queue target from 1024 frames to 512 frames also saves 512 frames of **nominal queued PCM** at a given clock. These are *software buffer quantities*, NOT independent guarantees of improved physical output/roundtrip latency. Smaller buffers increase host DSP callbacks per second and reduce CPU scheduling slack; a CPU-heavy VST can glitch even if the isolated test succeeds.

The user's actual shared WASAPI device previously reported period **480 frames=10ms** despite requesting 5ms. This experiment does NOT change Windows shared-mode device format/period.

## Implementation

- `src/asio_com_driver.cpp` now has instance-local `block_frames_`, default 512. On driver construction (not realtime) it optionally reads HKCU registry DWORD `Software\ModernSharedASIO\AsioBlockFrames`. Only values 256 and 512 are accepted; absent or invalid selects 512. **Changing this value requires closing Cubase and recreating the ASIO driver instance.**
- The selected size is consistently reported by `getBufferSize`, verified by `createBuffers`, used to allocate planar A/B output halves, applied to all host callbacks, actual PCM delivery, sample-position advancement, callback timing budget and producer queue policy.
- Queue target still defaults to two ASIO blocks; with 256 enabled it is **512 frames**. Existing separate optional `QueueTargetBlocks=1` can further shrink the target but should **NOT** be enabled during this initial experiment.
- Opposite-half handoff remains the hard invariant: callback half index means host writes it; hardware outputs the OTHER half. Previously user-observed future-bar audio was fixed by that convention.
- The WASAPI renderer constructor still receives the default source block-size argument (512), but the ASIO driver uses its **asynchronous source** mode. The renderer's legacy demand callback uses that argument only in synchronous mode, so it is not consumed by the ASIO path. The active ASIO producer uses `block_frames_` for requests.

## Portable regression

`tests/asio_block_modes_tests.cpp` runs:
- 5,000 callback A/B handoff and per-buffer PCM verification on **each** 256 and 512 frame mode, including sample positions `0, block, 2*block...`.
- 200,000 simulated WASAPI event cycles **per combination** of block size {256,512} and device period {480,441}, with two-block producer queue, no underrun in this idealized, jitter-free model, queue conservation.
- Local Linux Clang 17 `-fsanitize=address,undefined` test **PASS**. Does not simulate Windows scheduler, Cubase plugin delay compensation or true VST DSP.

CMake registers **18 Windows CTests** (17 prior phase-2 + this experimental mode regression), subject to full MSVC build. Windows build and real-device Cubase 256-frame results **NOT RUN**. No CI until final.

## Safe manual Windows A/B

**First**, keep the known-good 512 mode. Close Cubase. If there are local uncommitted files, preserve them before changing branches. Switch to the experiment, build, and inspect 18/18 tests:

```powershell
cd C:\ModernSharedASIO
git fetch origin
git switch experiment/asio-256-frames
git pull --ff-only
Remove-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name AsioBlockFrames -ErrorAction SilentlyContinue
.\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug
```

With no override, expect `getBufferSize` preferred=512, Audio at correct timeline position, MIDI/VST drawn notes, live keyboard and YouTube coexistence.

**Then** close Cubase, enable one variable only:

```powershell
New-Item -Path 'HKCU:\Software\ModernSharedASIO' -Force | Out-Null
New-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name AsioBlockFrames -PropertyType DWord -Value 256 -Force | Out-Null
```

Run registry-free smoke, which should print `block=256`, then test identical duplicate Cubase project without simultaneously changing QueueTargetBlocks, PeriodMs, sample rate or ASIO-Guard. Check for dropout, MIDI timeline shifts, Serum VST performance and general stability. Compare Debug `ASIO_STOP block_frames=256`, CPU utilization and xruns to baseline.

To revert safely, CLOSE Cubase and remove the registry setting, allowing 512 by default:

```powershell
Remove-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name AsioBlockFrames -ErrorAction SilentlyContinue
```

To restore the optimized main branch as well, `git switch main; git pull --ff-only; .\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug`. If the experimental DLL was rebuilt at the same registered Debug path, a new ASIO registration is unnecessary.

**Stop gate:** if any MIDI/VST playback issue, future-bar playback, device initialization failure or continuous underrun appears, immediately return to 512; do not declare 256 generally stable.

## Decision

Experimental code complete and portable model passes. **Not approved as main default and not merged**. Promotion only if real Cubase tests demonstrate no regressions and meaningful end-to-end or interactive-latency improvements.
