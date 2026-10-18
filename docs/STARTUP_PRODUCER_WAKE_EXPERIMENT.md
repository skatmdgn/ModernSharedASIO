# Intermittent first-WASAPI-render zero-queue experiment

2026-10-18 | Branch: `experiment/startup-producer-early-wake` | Base: `37ad45f9018a1788ebaa779337b662a17e45e6b5` | **Not merged to main.**

## Actual Cubase user evidence

Three ASIO_STOP runs at 48kHz, 512 ASIO frames, 1024-frame FIFO, 480-frame WASAPI render period, native stereo float32 direct output:

- 142 ms, 6 all-silent host callbacks: no underrun; log lacks new per-event keys (older DLL session).
- 1,509 ms, 138 all-silent host callbacks: **underrun_frames=480**, **underrun_events=1**, **largest_underrun_event_frames=480**, **first_underrun_render_event=1**, and `render_queue_min_frames=0`, `render_queue_below_demand_events=1`, `render_queue_first_low_event=1`. Max Cubase callback 135us, histogram all 138 callbacks <=500us, mean 46us, no host callback over budget. First render event ran without 480 device frames of ready FIFO PCM.
- 14,299 ms, 1,338 all-silent host callbacks: zero underruns, FIFO sampled min 1024 frames, max 1504, zero below-demand events. Max Cubase callback 599us, mean 54us.
- Every run had no backend stream/callback errors and `native_direct_frames=wasapi_submitted_frames`. These logs show an **intermittent startup FIFO-supply race**, not demonstrated sustained VST DSP overload. Because the offending run was all-silent there is no demonstrated audible gap in actual music.

## Source examination

`Driver::start()` creates the Cubase producer thread and then calls the blocking `render_.start()`. The latter starts the WASAPI worker, primes a silent device buffer, calls `IAudioClient::Start()`, and publishes initialization. **Only after `render_.start()` returns** does the driver call `SetEvent(producer_wake_event_)`. The first hardware render event can therefore run before the producer wakes, leaving the FIFO empty. This ordering is a concrete race possibility consistent with the observed first-event missing frames; proving exact scheduling needs a Windows trace.

## Experimental change — off by default

Add HKCU `Software\ModernSharedASIO\EarlyProducerWake` `REG_DWORD=1`. At ASIO driver `start()` after the producer worker is created, but **before** entering WASAPI `render_.start()`, signal the same producer wake event. This lets Cubase start preparing its normal alternating ASIO buffers while the Windows render worker initializes. Keep the original post-start wake as a fallback. Reuse exactly the existing Cubase producer and **opposite-half** output transfer; no extra pre-start submission, duplicated B buffer, sleeping or extra threads. No audio PCM, block sizes, queue targets, sample rate, ASIO TimeInfo or device period modifications.

**Risk:** this invokes Cubase callbacks earlier inside ASIO start, potentially before WASAPI device init completes. ASIO hosts may differ in callback expectations; **it is experimental, not enabled on main**. It may reduce the probability of the first-event underrun but does **not guarantee** scheduling or latency improvement. Switch off immediately if Cubase fails to start, timeline mismatches, crashes or glitches.

## Tests and status

New `modern_asio_early_prefill_tests` asserts two 512-frame callback handoffs (callback0 fills A -> output B; callback1 fills B -> output A) preserve sample timeline and FIFO ordering at a 480-frame WASAPI period. It tests deterministic data order, **not actual Win32 thread wake timing**.

Expected Windows CTest total is **20** on this branch (19 in main plus the experiment). Actual Windows MSVC build, 20/20 suite, Cubase A/B restarts and true first-event underrun elimination are **not yet verified**. CI deferred until final.

## Controlled A/B

Only if testing the experiment intentionally: close Cubase, save a backup of any uncommitted work, then:

```powershell
cd C:\ModernSharedASIO
git fetch origin
git switch experiment/startup-producer-early-wake
git pull --ff-only
.\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug
```

Default trial: leave `EarlyProducerWake` absent and restart Cubase several times using the same project. The log should say `early_producer_wake=0` and show whether the first render event sometimes underruns.

Opt-in trial: close Cubase, then set:

```powershell
New-Item 'HKCU:\Software\ModernSharedASIO' -Force | Out-Null
New-ItemProperty 'HKCU:\Software\ModernSharedASIO' -Name EarlyProducerWake -PropertyType DWord -Value 1 -Force | Out-Null
```

Start Cubase repeatedly with same settings, stop and check `early_producer_wake=1`, `underrun_events`, `first_underrun_render_event`, `render_queue_min_frames` plus actual Audio/MIDI timeline. Keep `QueueTargetBlocks` absent (1024-frame default), do not change `AsioBlockFrames`, TimeInfo or PeriodMs simultaneously.

Rollback without rebuilding: close Cubase and:

```powershell
Remove-ItemProperty 'HKCU:\Software\ModernSharedASIO' -Name EarlyProducerWake -ErrorAction SilentlyContinue
```

Return to existing `main` or the user-verified stable phase-2 source if any regression. Original Steinberg ASIO DLL and registry untouched.

## Local standalone C++ handoff check (2026-10-18)

The new early-prefill data-order test was compiled and executed locally with Clang C++20 AddressSanitizer + UndefinedBehaviorSanitizer (`-O1 -g -fsanitize=address,undefined`) and GCC C++20 optimized (`-O3 -DNDEBUG`): both printed `early producer prefill preserves ASIO opposite-half PCM order: PASS`. The local build used the existing local phase-1 FIFO/bridge header copies and the small matching timeline/opposite-half headers; it is a portable **logic-path regression only**. The actual Windows DLL, OS event scheduling, Cubase playback and complete 20-CTest suite remain **unverified**.

## 2026-10-18 user on-Windows early wake ON actual logs

First real Cubase data for this experimental branch: user enabled `EarlyProducerWake=1` and supplied 3 stop records, all on ASIO 512 frames, queue target 1024 frames, 48 kHz, WASAPI period 480, native stereo Float32 direct output:

| Run | Wall duration | Host PCM | Callback peak | Over-budget | Underrun frames / events | First underrun render event | Queue min |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 14,659 ms | all silence | 712 us | 0 | 0 / 0 | none | 1024 frames |
| 2 | 48,420 ms | 3947 nonzero L/R callback blocks (591 silent) | 23,072 us | 4 | **480 / 2** | **276** | **96 frames** |
| 3 | 128 ms | all silence | 95 us | 0 | 0 / 0 | none | 1024 frames |

Loaded run 2: max per-event deficit 384 frames and total deficit 480 across exactly 2 events, so the other event missed **96 frames**. Callback timing histogram 405,3686,428,11,3,2,1,2 (sum=4538); callback mean 788us, longest consecutive over-budget streak 1 and first over-budget block ordinal 225. FIFO pressure occurred in 2 events, first at WASAPI render ordinal 276; native_direct_frames = submitted_frames = 2,322,816; no callback/backend errors.

Interpretation: **there were no first-event underruns among these three starts**, consistent with the early wake potentially helping initialization. This does not prove elimination of an intermittent startup race. The 480 missing music-session frames occurred **later in playback**, not during the first event. Two isolated event deficits do not establish which precise VST callback or Windows scheduling gap caused them; over-budget host callback activity makes workload/scheduling a plausible candidate. Audio monitoring subjective status was not explicitly reported for this batch. The three sessions are not a controlled OFF-vs-ON A/B under identical load, so do not assert early wake causes better or worse steady-state playback.

**New separate correctness concern surfaced:** user run 3 had `last_reported_latency_frames=1024` whereas runs 1 and 2 had 2080. Source `getLatencies()` returns `2*kFrames + render_.device_info().buffer_frames`: before the renderer publishes device info, its buffer_frames=0 so value=1024; after initialization it equals 1056 so returns 2080. Thus the ASIO host can observe different provisional latencies depending on request timing. Not evidence of physical latency changing. Do not silently change the value in this startup branch; address with a separate deliberate hardware/reporting contract investigation.

**Decision:** keep experiment isolated; retain known-good main and phase-2 stable branch. Continue only with matched repeated OFF/ON tests before promoting early wake, and separately investigate a stable, defensible getLatencies contract.
