# Architecture — Batch 1

```text
[Future Cubase 8 IASIO callbacks, NOT IMPLEMENTED]
                |
[Future ASIO format/clock/period bridge]
                |
[SpscStereoRing: stereo float32, single writer/single reader]
                |
[Dedicated Windows WASAPI renderer worker + MMCSS]
                |
[IAudioClient3 shared engine, or new IAudioClient shared fallback]
                |
[Windows default output endpoint]
```

## Core invariants

1. NEVER open `AUDCLNT_SHAREMODE_EXCLUSIVE`; coexistence with YouTube is mandatory.
2. SPSC push/pop have no allocations, blocking waits or mutexes. Initialization and stop can block on control thread.
3. WASAPI COM instances are created and destroyed in one worker's COM apartment.
4. Stop events signal the render worker and `join` before resource cleanup; do not free in-use worker state on an unchecked 3-second timeout.
5. IAudioClient3 period comes from `GetSharedModeEnginePeriod` default/fundamental/min/max constraints. If unavailable/rejected, activate a fresh IAudioClient in SHARED mode.
6. The selected ASIO buffer size is NOT assumed equal to the Windows engine period. Clock drift and scheduling need explicit bridge design.
7. Windows mix format is detected at runtime. The renderer currently understands IEEE float32 and PCM16/24/32, with stereo source routed to first two physical output channels only.
8. Default device is eConsole, eRender. Device re-open on errors/hotplug is future work. A successful silence smoke does not validate xrun-free DAW operation.

**Validation:** local portable tests PASS; Windows build and runtime NOT_TESTED. No native ASIO DLL yet.

## Batch 3 scheduling revision

The independent `std::chrono::sleep_until` ASIO callback thread has been removed. WASAPI worker render events now request missing 512-frame ASIO blocks through `fulfill_audio_demand`; no producer callback is invoked if sufficient frames are queued. Work per event is bounded by FIFO capacity and stalls are detected. MMCSS is acquired before first host callback. Periodic IAudioClock measurements are diagnostic only; `getSamplePosition()` and ASIO timeinfo still use generated-frame count/QPC, not a calibrated hardware clock. This is an experimental event-paced architecture, not verified low-latency performance.

## Batch 5: production/render thread separation (source only)

```text
WASAPI event callback (MMCSS high)
   -> read SPSC FIFO (never Cubase DSP)
   -> ReleaseBuffer()
   -> SetEvent(producer_wake_event)
                    |
                    v
ASIO producer thread (MMCSS normal, event-driven)
   -> policy: fill up to 1024 queued frames (2 x 512)
   -> Cubase bufferSwitch/bufferSwitchTimeInfo callback
   -> packed Float32 stereo -> preallocated SPSC
```

A newly introduced scheduling difference: FIFO starvation can still cause silence because the render worker never waits for Cubase. This is deliberate to protect Windows audio deadlines, and MUST be measured on hardware. The target of two ASIO blocks is provisional, not a measured optimum. Producer thread is joined before its events/buffers can be released. If the host callback hangs, stop may block indefinitely; do not call it production-safe. No Windows build/runtime test yet.
