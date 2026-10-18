# Optional scheduler gap diagnostics (Phase 3)

2026-10-18 | development branch `optimization/full-pass-20261018` | DEFAULT OFF

## Motivation

User logs on 48 kHz / 512 ASIO / FIFO 1024 showed occasional 18-23ms Cubase callback spikes and isolated audio FIFO underruns. The ASIO host callback timer alone cannot attribute whether OS scheduling, host VST DSP, or a delayed WASAPI device event caused those drops. Avoid raising MMCSS priority, increasing FIFO, or reducing ASIO buffers on guesswork.

## Explicit opt-in — no new QPC probes when disabled

Before Cubase starts, set DWORD `HKCU\Software\ModernSharedASIO\EnableSchedulerProbe` to `1`. The driver reads it once in `start()` and configures the renderer before starting its worker. No extra per-render or per-wake QueryPerformanceCounter calls when absent or zero; a simple boolean gate remains. The existing callback duration QPC measurements are unchanged.

```powershell
New-Item 'HKCU:\Software\ModernSharedASIO' -Force | Out-Null
New-ItemProperty 'HKCU:\Software\ModernSharedASIO' -Name EnableSchedulerProbe -PropertyType DWord -Value 1 -Force | Out-Null
```

With the option on, read these new ASIO_STOP keys:

- `scheduler_probe=1`: opt-in active. `scheduler_probe=0` means OFF.
- `producer_wake_gap_samples`, `producer_wake_gap_mean_us`, `producer_wake_gap_max_us`, `producer_wake_gap_over_budget`: estimated time between the MOST RECENT WASAPI->producer SetEvent request and the producer returning from `WaitForMultipleObjects`. Over-budget threshold uses ASIO block duration at active sample rate (10.67ms at 512 / 48k). Measurements skip missing/invalid initial timestamps.
- `render_event_gap_samples`, `render_event_gap_max_us`, `render_event_gap_over_1p5x`: time between successive audio-event wakeups on the WASAPI render thread. For valid IAudioClient3 `period_frames`, delayed events are intervals strictly greater than 1.5x the configured engine period (15ms for 480 frames / 48k). If the engine does not provide a valid `period_frames`, over-1.5x counts are not available, although maximum interval may still be reported.

These measurements are NOT proven causal attribution of an underrun, roundtrip latency or DPC. Windows auto-reset events coalesce signals and worker CPU scheduling can update the last request timestamp: producer wake times are estimates, NOT exact signal->callback durations. A large event gap can reflect render work, scheduling or normal device behavior, not necessarily a DPC problem.

Read the last few stop records with PowerShell:

```powershell
.\scripts\summarize_asio_stops.ps1 -Last 20
.\scripts\summarize_asio_stops.ps1 -Last 20 -OnlyAudio
```

The summary tool only reads the existing log. It does not reset it, uninstall anything or alter ASIO settings. `-OnlyAudio` suppresses sessions where `host_nonzero_left=0`. To clear the log, only do so intentionally while Cubase is closed; the default tool never deletes historical logs.

To switch probing OFF:

```powershell
Remove-ItemProperty 'HKCU:\Software\ModernSharedASIO' -Name EnableSchedulerProbe -ErrorAction SilentlyContinue
```

Close and reopen Cubase after changing the registry because start() reads the setting once.

## Tests and safety boundary

- Portable `tests/scheduler_gap_tests.cpp` verifies invalid/zero QPC inputs are ignored, 480/600/2500us duration arithmetic, strict over-threshold counting, max, first ordinal, concurrent snapshots during 100,000 updates, and reset.
- The standalone test was actually compiled and executed locally with Clang 17 C++20 ASan+UBSan and GCC C++20 Release: both PASS.
- CMake now registers **26 expected Windows CTests** on the ASIO DLL build (25 prior + 1 gap tracker test); full MSVC driver build, 26/26 tests and real Cubase integration **NOT YET RUN**. PowerShell summary script has not been parsed by pwsh in this Linux environment; inspect/test on actual Windows PowerShell.
- No changes to Audio/MIDI PCM, ASIO opposite-half handoff, Cubase callback timing, ASIO block 512, FIFO target 1024, WASAPI device period, `getLatencies`, original Steinberg driver, host registration, MMCSS priority, 256-frame experiments, or startup early-wake experiment.
- The optional probe performs extra QPC queries and atomic writes while enabled; it is meant for diagnosis, not as a standalone performance optimization. Leave disabled for normal performance A/B.

## Suggested measured test

1. With the complete experimental full-pass driver built/tested on Windows, leave probe OFF and play the *same* actual music/VST project for at least 90s; check Audio and sequenced MIDI timing, live keyboard, concurrent browser audio, and undetected PCM underflow.
2. Repeat with probe ON without changing ASIO 512, FIFO 1024, Cubase ASIO Guard, PeriodMs or sample rate; compare actual underrun_frames/underrun_events and callback distribution. Probe should not be used to claim CPU improvement because it adds diagnostic overhead.
3. If producer_wake_gap_max_us is high while max_host_callback_us remains low and render_event_gap_max_us is normal, investigate producer worker scheduling. If Cubase host callback spikes >10.67ms but wake gaps stay small, profile VST DSP using Windows Performance Recorder/Analyzer. If render_event_gap_max_us is high, inspect WASAPI/device event pacing. These are hypotheses, not proofs.
4. Restore the known-good `stable/cubase8-phase2-1024-confirmed` branch and rebuild if Windows tests or audio content regress.

