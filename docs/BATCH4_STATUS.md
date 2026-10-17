# Batch 4 — startup isolation, diagnostic separation and Windows preflight

Date: 2026-10-18. All original Steinberg/ASIOGLLD files untouched. No CI.

## Changes
- Renderer initial priming fills only previously queued audio plus intentional silence; Cubase callbacks are never invoked by the priming operation.
- Playback active state is atomically set before WASAPI initialization can activate render events, and reset on failures. This protects createBuffers/disposeBuffers during startup; general concurrent host operations remain unsupported.
- Producer callback failures, ongoing missing-frame counts and WASAPI stream errors are tracked separately.
- Explicit opt-in Windows x64 build script and registry-free COM preflight CTest. No automatic registration.

## Actual verification
- Portable Clang17 C++20 Debug with -Wall -Wextra -Wpedantic -Werror and AddressSanitizer/UndefinedBehaviorSanitizer: 5/5 CTest pass. Core, bridge, demand, 44.1/48k simulation and new startup isolation test.
- Windows ASIO DLL build, WASAPI playback and Cubase 8.0.40 runtime: NOT TESTED (Windows SDK is absent in this execution environment).

## Remaining
- MSVC x64/official Steinberg SDK compilation plus actual COM preflight, Windows smoke output and Cubase+YouTube host tests.
- Hardware-clock time/latency calibration, deadline instrumentation and isolation of long host callbacks from WASAPI device worker, device recovery, recording, rate conversion, multiple endpoints.
- Current prototype must NOT be installed or presented as a production-quality ASIO driver.
