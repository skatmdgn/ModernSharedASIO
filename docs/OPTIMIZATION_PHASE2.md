# Optimization Phase 2: Direct native WASAPI Float32 stereo output

Date: 2026-10-18 (KST). Repo stable rollback reference: `stable/cubase8-verified-20261018` at `19f79a9`. Phase-1 user-tested main `9cde0e5`. Original Steinberg ASIO driver must remain untouched.

## Target and boundary

The Windows shared-mode renderer previously did the following even when the endpoint's format was **native stereo IEEE Float32**, a case `convert()` ultimately handles with a plain `memcpy`:

```
Cubase IASIO A/B (opposite-half hardware output contract, preserved)
    -> planar-to-interleaved SPSC FIFO
    -> render worker: FIFO.pop -> preallocated float scratch
    -> render client GetBuffer -> memcpy scratch to Windows device buffer
    -> ReleaseBuffer
```

New **only when `asynchronous_source_==true`, `fmt.kind==Kind::float32`, and `fmt.channels==2`**:

```
Cubase IASIO (unchanged) -> SPSC FIFO
    -> WASAPI GetBuffer -> FIFO.pop directly into native float32 device buffer
    -> zero-fill underrun tail -> ReleaseBuffer
```

All other output types (PCM 16/24/32, multichannel float32, synchronous diagnostic sources) retain the original staged/converted path, including its source-callback-before-device-buffer ordering. This avoids executing synchronous host DSP while a render client buffer is held.

The directly acquired native device buffer is obtained *before* consuming FIFO samples, so failed `GetBuffer` does not drop PCM. There is a single `GetBuffer` / `ReleaseBuffer` pair for each populated device period, and no allocation inside the render loop. For `frames > queue.capacity_frames()`, the existing sub-batch loop remains in place; only missing frames are zeroed, exactly as before.

**Non-negotiable invariant:** ASIO double-buffer processing/output must remain `bufferSwitch(0) -> hardware half B` and `bufferSwitch(1) -> hardware half A`. No changes were made to that logic, ASIO block size (512), clock, TimeInfo configuration, queue target (default 1024), requested WASAPI period, Cubase callbacks, latency reports, or registration.

## Changes

- `include/modern_asio/float32_output_transfer.hpp`: bounded direct transfer helper that copies available frames and explicitly zero-fills missing frames; returns actual copied and silent counts.
- `src/wasapi_shared_renderer.cpp`: native path is gated by actual renderer format and async Cubase source; scratch only allocated for legacy/fallback formats; native output holds device buffer only for bounded FIFO copy and zero-fill.
- `include/modern_asio/wasapi_shared_renderer.hpp`: `native_direct_frames` counter in stats.
- `src/asio_com_driver.cpp`: `ASIO_STOP` logs `native_direct_frames`, so a user can confirm which path ran. When `native_direct_frames=0`, this optimization wasn't active, typically because the endpoint wasn't native float32 stereo (or stream was not async).
- `tools/shared_mode_smoke.cpp`: prints the additional counter (normal standalone smoke is synchronous and will typically print zero).
- `tests/float32_direct_output_tests.cpp`: differential comparison with the old FIFO->scratch->memcpy algorithm, 60,000 random cases over 2/8/16/512/1024/32768-frame capacities, including wrap/partial read/underflow and guard sentinels. Additional test for device output bigger than FIFO capacity.
- `tools/native_float32_bench.cpp`: optional native transfer microbenchmark; not included in CTest and requires `MSA_BUILD_BENCHMARKS=ON`.

## Validation actually performed

- Clang 17 on Linux, C++20, `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`: new differential test PASS, 60,000 cases plus large-buffer chunk test.
- Existing phase-1 planar SPSC ASan/UBSan regression and 441/480-frame producer policy simulation also rerun successfully, including 250,000 concurrent PCM frames.
- Clang 17, `-O3 -DNDEBUG` native-stereo memory-path benchmark (1056 device frames, 200,000 repetitions per run, alternating four staged/four direct runs): staged **0.3095us/call**, direct **0.2173us/call**, **1.425x** in this narrow path.
- **No Windows WASAPI/Cubase benchmark yet.** This is an improvement in a sub-microsecond memory-copy microbenchmark, **not** a 1.425x increase in Cubase performance, not an established lower roundtrip latency, and not evidence of fewer audio dropouts.
- CMake now registers **17 Windows CTests** if `MSA_BUILD_ASIO_DLL=ON` (16 prior + one direct-output test). Windows MSVC compilation and actual Cubase runtime verification are still pending. CI deferred until final.

## Manual Windows check

1. Make a copy of your Cubase project, close Cubase, and build at the same previously registered DLL path:

```powershell
cd C:\ModernSharedASIO
git pull --ff-only
.\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug
```

2. Expect **17/17 tests**. No ASIO re-registration is required when registry paths remain unchanged. Re-open Cubase and specifically test (a) Audio clip at correct bar position, (b) recorded/drawn MIDI/VST notes, (c) live master keyboard, (d) simultaneous YouTube audio and Cubase, (e) playback under many VST tracks.

3. After exiting or stopping the ASIO driver, check:

```powershell
Get-Content "$env:TEMP\ModernSharedASIO-debug.log" -Tail 3
```

Expect nonzero `native_direct_frames` when Cubase opened a native float32 stereo Windows endpoint. Also verify `underrun_frames=0`, `stream_errors=0` and `callback_errors=0` for healthy sessions. **Do not assume zero native_direct_frames means a failure:** a PCM/multichannel output format correctly exercises the safe fallback.

4. If playback regresses, close Cubase and restore the known-good `stable/cubase8-verified-20261018` branch, rebuild, then restart Cubase. Preserve any local changes before switching branches. The optimized `main` remains available for subsequent debugging.

## Next priorities (only after real-device acceptance)

1. Measure the true signal and control-path latency, not merely ring occupancy. Stabilize `getLatencies` reporting across device init/start.
2. Introduce **opt-in 256-frame ASIO buffer** only with adapted timing, A/B output mapping and VST stress regression. Avoid changing the proven 512-frame default.
3. Profile Cubase/DPC scheduling jitter and producer/event wakeups under 1/10/50 tracks. Consider host callback scheduling tuning only with evidence.
4. Test actual output device formats, reconfiguration, long-run playback and failed-start/teardown behavior.

## Real Cubase session update — 2026-10-18

User confirmed direct native Float32 output worked and sound was correct. All `native_direct_frames` equaled submitted frames. Crucially, `audio_buffer_target_frames=512` (experimental QueueTargetBlocks=1) with one loaded session yielding 2976 underrun frames across 48.635 seconds; callback max 30371us, 4 callback overruns. This **fails a zero-underrun stability gate** despite no audible issue. See [actual Cubase underrun review](CUBASE_PHASE2_UNDERRUN_REVIEW.md). Do not promote one-block mode as default. Event-level telemetry added; full Windows retest pending.

## User follow-up: default 1024-frame target sustained loaded playback without underruns

After restoring `audio_buffer_target_frames=1024`, 76.043s loaded Cubase session reported **underrun_frames=0** despite worst host callback 18.608ms and 3 callbacks exceeding the normal 10.67ms ASIO block deadline. 100% of 3648576 submitted frames took the native direct output path. With 512-target earlier, 2976 frames were missing over 48.635s, with a much larger worst host callback of 30.371ms. Evidence favors retaining 1024 default, but is not controlled DSP workload, so the queue setting is not conclusively the only cause. Source revision with event-based telemetry was NOT yet running (no new fields in logs). See docs/CUBASE_PHASE2_UNDERRUN_REVIEW.md.
