# Cubase Phase-2 user logs: experimental 1-block queue has dropouts in counters

Date: 2026-10-18 (KST). Applies to Cubase 8.0.40, 48 kHz, `main` phase-2 direct native WASAPI Float32 renderer commit `074edfee9d062a8d50d33b80052f438f14f8b191`.

The user said **audio sounds fine**, and pasted three ASIO_STOP records. These demonstrate functional playback correctness and successful native Float32 transfer, but **NOT glitch-free output** under the selected experimental queue target. The active queue target was 512 frames (not the two-block 1024-frame default), so at least the `QueueTargetBlocks=1` setting was in effect.

| Session | wall duration | ASIO callbacks | nonzero host blocks | worst Cubase callback | over 512-frame time budget | underrun frames | native_direct_frames |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A | 30,820ms | 2,886 | 0 | 5,123us | 0 | 0 | 1,478,016 |
| B, audio playing | 48,635ms | 4,548 | 3,950 | **30,371us** | **4** | **2,976** | 2,331,936 |
| C | 171ms | 10 | 0 | 313us | 0 | 0 | 5,376 |

Each session had `rate=48000`, `block_frames=512`, `engine_period_frames=480` (10ms), `wasapi_buffer_frames=1056`, `audio_buffer_target_frames=512`, `asio_time_info=0`, `disable_time_info=1`, `last_reported_latency_frames=2080`, `stream_errors=0`, `callback_errors=0`, `producer_failures=0`, `last_hr=0`. In each row `native_direct_frames == wasapi_submitted_frames`, confirming that **all submitted audio was handled through the direct native Float32 path** in these sessions.

Session B had `2976 / 48000 = 0.062s` cumulative missing audio, **~62 milliseconds total**, `2976 / 2331936 = 0.1276%` of submitted frames. This is a sum across the session; the log does not disclose the number or length of individual dropout events. The worst host callback took **30.371ms**, versus a block time budget of **512/48000 = 10.667ms** (2.85x). Four callbacks exceeded budget. This may account for scheduling starvation but exact coincidence cannot be proved from session aggregate counters.

**Conclusion:** Phase-2 direct output is enabled and the user hears normal playback. However, at the experimental 512-frame producer queue target a loaded session did experience underruns. Do **NOT** promote QueueTargetBlocks=1 as stable or call the system glitch-free. The safe default remains QueueTargetBlocks absent (two ASIO blocks = 1024 frames).

## Immediate controlled follow-up

1. Close Cubase and REMOVE just the QueueTargetBlocks HKCU override (do not change TimeInfo, WASAPI period, ASIO frame size, audio routing, VST project or 48k sample rate):

```powershell
Remove-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name QueueTargetBlocks -ErrorAction SilentlyContinue
```

2. Restart Cubase, run the same music/VST project for at least 60 seconds and compare `ASIO_STOP` counters. Expect `audio_buffer_target_frames=1024`. Prefer `underrun_frames=0`, `stream_errors=0`, and `callback_errors=0`, plus correct placed Audio/MIDI and live-controller output.
3. If 1024 still underruns, investigate heavy-VST CPU/scheduler spikes, MMCSS priority interactions, real WASAPI timing and source callback distribution before increasing buffering. A 30ms host callback spike can exceed even the 1024-frame target.
4. Note that `last_reported_latency_frames=2080` is still provisional and does not track this queue setting. Do not claim that a shorter queue automatically makes Cubase's reported latency accurate.

## Added source instrumentation (no PCM behavior change)

`UnderrunEventTracker` records one event **per WASAPI render request** with one or more missing FIFO batches; tracks `underrun_events`, `largest_underrun_event_frames` and `first_underrun_render_event`. A render event with zero missing frames is not counted; startup priming with `allow_host_callback=false` is ignored, matching existing `underrun_frames` behavior. No new sleeps, locks, heap allocations, or changes to producer/consumer PCM. A portable unit test covers zero events, first event, max deficit, concurrent reader, and reset.

New stop log fields are appended after existing fields, so old log parsers should still be able to read the original keys. The previously reported 2,976 frames cannot be broken down retroactively; only logs from the rebuilt driver will contain event-level summaries.

**Verification status:** User real playback confirmed on previous phase-2 binary. The instrumentation code is source-level and its Windows MSVC test/Cubase results are pending. Total CMake test registration becomes **18** (17 previous + new event test). CI remains deferred until final.

## Portable instrumentation verification

After committing, the new `include/modern_asio/underrun_event_tracker.hpp` and `tests/underrun_event_tests.cpp` were compiled and executed locally under Clang C++20 (`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread`): **`Underrun event instrumentation PASS`**. This covers the standalone counter only, **NOT the Windows WASAPI integration, MSVC build, full 18-CTest suite, or Cubase performance**. The machine cannot directly clone GitHub due to DNS/network restrictions; the exact new portable source was separately compiled. All Windows checks remain outstanding.
