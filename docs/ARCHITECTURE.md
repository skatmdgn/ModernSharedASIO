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
