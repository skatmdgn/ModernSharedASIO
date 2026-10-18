# Batch 6 — coherent device clock and diagnostics

Date: 2026-10-18. Target: Windows x64, Cubase 8.0.40 + concurrent browser audio.

## Verified source changes

- New clock_observation.hpp carries device ticks, IAudioClock::GetFrequency, and the QPC timestamp from the same successful IAudioClock::GetPosition call. QPC is 100ns, not raw performance ticks.
- Single-writer atomic observation channel with sequence check to avoid torn/mismatched snapshots. The render worker publishes only exact S_OK, nonzero-QPC readings; degraded S_FALSE is not accepted.
- Integer time conversions use quotient/remainder and explicit overflow guards; rejected pathological clock data cannot silently overflow.
- OutputStats now tracks frames submitted to IAudioRenderClient (including intentionally padded startup silence). The smoke tool prints coherent played-frame estimates, their paired ASIO-compatible ns timestamp, and approximate submitted-minus-played frames.
- Last renderer HRESULT preserved to tell device/render errors from timeouts; streaming errors are not only counted.
- CMake registers the new clock_observation test. Full registered test suite has not been rerun.

## Executed local validation

- Linux Clang C++20 strict warning compilation, ASan/UBSan: PASS.
- Linux Clang optimized -O3 -DNDEBUG release: PASS.
- 400,000 writer publications with simultaneously reading threads: no incoherent snapshot observed.
- 100,000 deterministic random tick/frequency/rate comparisons against 128-bit exact division: PASS.
- Tests were run on the standalone portable header/test and mirrored to repository; **not** on native WASAPI/COM code.

## Windows build/host status and blockers

- Windows cross-compiler and Windows SDK headers/libraries are unavailable in this runtime; git clone to local environment was also blocked by disabled outbound network. Cannot claim an MSVC x64 DLL build or COM preflight.
- GitHub Actions/CI deliberately not run, consistent with the project's final-CI-only workflow.
- No Cubase 8.0.40 or YouTube coexistence test performed; no measurable audio latency or stability improvement asserted.
- Next task: build Debug/Release on Windows using scripts/build_windows_x64.ps1 + official external ASIO SDK; correct any MSVC compile errors, then registry-free COM preflight and device smoke.
- Playback stream tick → ASIO buffer samplePosition offset is not calibrated, because pre-roll and startup silence must be mapped first. The new conversion is instrumented diagnostics, not yet end-to-end timestamp correction.
- Producer DSP thread can block if the ASIO host callback hangs; COM lifetime and thread shutdown under host Release() still need stress testing.
- Existing original Steinberg DLL / ASIOGLLD repository unchanged.

## Microsoft API reference

https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition
