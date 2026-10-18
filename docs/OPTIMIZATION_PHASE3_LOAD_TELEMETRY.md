# Phase 3: realtime callback/queue-pressure telemetry

Date: 2026-10-18. Source base f209641f2a2ccd86b97d8db7ae1122bcc5c6aaa4.
STATUS: source instrumentation only; not a Windows/Cubase performance fix.

## User evidence

- At 48kHz/512 ASIO, experimental FIFO target 512 frames: 48.635 seconds, 2,976 missed PCM frames (~62ms total), maximum ASIO host callback 30,371us and 4 over-budget callbacks.
- Same general project on default FIFO target 1024 frames: 76.043 seconds, 0 missed frames, maximum callback 18,608us, 3 over-budget callbacks.
- Both used direct WASAPI Float32 output and Audio/MIDI sounded correct. The loads are not perfectly matched, so buffer target alone is not proven to account for the difference.

## Exact data added to ASIO_STOP

- cb_time_hist_500_1k_2k_4k_8k_12k_20k_us: 8 counts in duration ranges <=500us, (500,1000], (1000,2000], (2000,4000], (4000,8000], (8000,12000], (12000,20000], >20000us. Exactly one bucket per successfully returned Cubase ASIO callback.
- cb_mean_us: simple arithmetic mean of host callback wall durations, not total DAW CPU utilization.
- cb_longest_overbudget_streak: maximum consecutive callbacks with elapsed >=512*1e6/sample_rate microseconds.
- cb_first_overbudget_block: first block ordinal that exceeded that budget (UINT64_MAX means none).
- render_queue_samples, render_queue_min_frames, render_queue_max_frames: FIFO available frames sampled at nonempty WASAPI render requests, excluding startup priming.
- render_queue_below_demand_events and render_queue_first_low_event: count and first render event where sampled FIFO frames were below the requested device frames. These values are queue-pressure hints, NOT confirmed audio underruns: ASIO may replenish concurrently. Use actual underrun_frames and underrun_events to evaluate confirmed missing PCM.
- wasapi_stream_latency_100ns: already observed IAudioClient GetStreamLatency estimate, not a physical end-to-end or roundtrip latency and NOT used to change Cubase getLatencies().
- Existing ASIO_STOP fields remain unchanged. Log array enlarged 1200 -> 2400 bytes to prevent truncation.

## Implementation guardrails

CallbackTimingTracker is single writer (Cubase producer) and read only after stop has joined that thread, avoiding new atomic operations, locks or allocations within the ASIO callback. RenderQueueTracker uses relaxed atomics to permit safe stats reads while WASAPI runs. Neither changes any audio PCM, device buffer, producer policy, ASIO buffer alternation, sample time, ASIO outputReady or MMCSS.

Default mode remains 512 ASIO frames, 1024 FIFO frames (QueueTargetBlocks missing/2), TimeInfo disabled unless explicitly opted in. Experimental 256 ASIO block branch stays unmerged.

## Verification status

- Exact standalone portable new header/test compiled and ran with Clang C++20, ASan + UBSan, -Wall -Wextra -Wconversion -pedantic: PASS.
- The same tracker test built and ran with GCC C++20 -O3 -DNDEBUG: PASS.
- Covers 8 duration bucket boundaries, over-budget streaks, queue first/min/max, concurrent reader over 100000 producer observations and reset.
- Expected Windows CMake total is 19 CTests (18 existing + modern_asio_realtime_load_tests). Full Windows MSVC compilation, 19/19 tests and actual Cubase test on the new DLL are NOT VERIFIED.
- Telemetry has nonzero but small runtime overhead. It does not itself reduce latency.

## Windows user validation

Close Cubase and execute from PowerShell (only ModernSharedASIO, NEVER edit Steinberg's proprietary original DLL):

    cd C:\ModernSharedASIO
    git switch main
    git pull --ff-only
    Remove-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name QueueTargetBlocks -ErrorAction SilentlyContinue
    .\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug

Run the same Cubase project for at least ~90 seconds. Confirm that Audio clips, sequenced MIDI, live master keyboard and Windows shared audio are correct. Check that the queue target is 1024 and underruns, stream/callback errors are zero. Read the latest stop log:

    Get-Content "$env:TEMP\ModernSharedASIO-debug.log" -Tail 3

Known-good phase-2 binary source preserved as branch stable/cubase8-phase2-1024-confirmed at 074edfee9d062a8d50d33b80052f438f14f8b191. Earlier verified baseline preserved as stable/cubase8-verified-20261018. CI deferred until final.

## Next decision

Use real callback histogram, longest consecutive over-budget streak, render FIFO pressure and actual underruns to identify whether high-load behavior is host-DSP-bound or producer-scheduling-bound. Physical output/loopback measurement is required before changing provisional 2080-frame getLatencies reporting. Do not blindly reduce buffers.
