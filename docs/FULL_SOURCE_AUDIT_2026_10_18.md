# ModernSharedASIO — whole-source static audit of Cubase future-bar playback

**Date:** 2026-10-18 (KST). **Repo target:** main at da4580874c3a03f2795c705e2fbbdd3ec0d9a15c, with the changes described below staged. **Important:** static source inspection is not an end-to-end Windows or Cubase runtime proof. Do not declare the user's ~16-bar source displacement resolved.

## Exact symptom and existing measured evidence

- The user places Cubase playhead at bar 0, where the arrangement contains **no source clips/audio**, but hears musical material situated about **16 bars later**. Single-track projects are more stable; adding tracks makes the issue and VST/Serum silence worse. This is *future-content-at-zero*, **not** 'sound delayed by 16 bars'.
- Prior 48kHz Debug logs: 512-frame ASIO callback, WASAPI period 480 (10ms) even though requested 5ms, WASAPI buffer 1056, output reported 2080 frames; three runs 6417 / 5341 / 5 callbacks, 0 backend underruns or stream errors, with both all-silent and partly nonzero host ASIO output. Host silence counts are session aggregates with no Cubase transport flags; must not be mapped to project bars.
- These logs predate the new clock-pair and buffer-scrubbing candidate; the prior ASIOOutputReady and +512 sample-position fixes did not fix the audible symptom.

## Static code audit matrix

| Component | Source and cross-check | Result |
| --- | --- | --- |
| COM factory/registration | `src/asio_com_driver.cpp`, `.def`, `scripts/register_cubase_test.ps1`, unregister script, ASIO SDK host code | Separate CLSID, no edits to Steinberg asioglld.dll. Registration must be user-run as admin; existing protected scripts guard collisions. Cubase has already loaded the driver, so COM enumeration isn't the musical bar error. |
| ASIO buffer creation | `Driver::createBuffers`, channel tests, ASIO SDK output contract | Stereo output-only, planar Float32LSB, two allocated halves, stored host callbacks. No out-of-bounds access identified for validated channel/count values. |
| Startup/pre-roll | `Driver::start`, `render_available(false)`, startup test | Initial output B prefill is **512 frames**. A real/expected initial block can be silent; not 16 bars. |
| Per-callback index/position | `produce_one_block`, `AsioCallbackTimeline`, manual mini-host | Alternates 0/1 and advances exactly 512 frames per completed callback. The prior +512 ASIO contract bug was already fixed and manually verified. |
| Physical streaming | `WasapiSharedRenderer::thread_main`, event loop, format converter | Uses WASAPI shared mode, GetCurrentPadding, one render event loop. WASAPI itself takes a bounded device buffer and cannot store 16 bars of user audio. One requested 5ms period resulted in actual 10ms on user's endpoint. |
| FIFO and PCM conversion | `SpscStereoRing`, `AsioPlaybackBridge`, producer fill policy, `transport_content_order_tests` | FIFO capacity **32768 frames = 0.683 seconds at 48kHz**, producer fill target **1024 frames = 21.33ms**; 16-bar marker simulation preserves sample order. Thus the ordinary path cannot hold ~16 bars of audio. |
| Plugin callback/producer | `producer_loop`, `produce_one_block` | Cubase host callbacks run on a dedicated Pro Audio MMCSS producer thread, not Windows render worker. `directProcess=ASIOTrue` requests immediate processing; major host-specific scheduling or re-entrancy effects remain unverified. |
| Time information | ASIO SDK 2.3 `common/asio.h`, Driver::getSamplePosition/timeInfo | **CONFIRMED source-contract defect:** existing `getSamplePosition` returned fixed block index but freshly sampled QPC timestamps, not a latched pair. `ASIOTimeInfo.systemTime` also used QPC, whereas SDK says on Windows it should derive from `timeGetTime`. Source corrected with one coherent atomic block stamp and Windows timeGetTime nanoseconds; tests added. Still cannot claim hardware-clock calibration. |
| Planar output reuse | `planar_[channel][half]` and `produce_one_block` | **CONFIRMED stale data possibility:** output halves retained old PCM between callbacks and were submitted even when host didn't rewrite them. Now zero each active half **before** host callback, preserving the startup B prefill and host ability to write. This is defensive, not proof that Cubase actually left samples unwritten. |
| Reported latency | `getLatencies` | **OPEN:** pre-start call can report 1024 frames while after renderer init it may report 2080 (user log). This is a real inconsistent ASIO latency report and needs redesign, but 1056-frame difference =22ms, not ~16 bars. Do not 'fix' via invented device buffer size. |
| Host transport and ASIO-Guard | ASIO interface, Cubase project state | **NOT ACCESSIBLE** through basic ASIO output callback API; no Cubase transport/bar events captured. Existing mini-host tests do not simulate Cubase ASIO-Guard or actual plugin mixdown. Need actual host/loopback source correlation to explain future contents. |
| Sample rate and device change | Driver::canSampleRate, init, WASAPI endpoint | Supports current Windows mix sample rate only; no sample-rate conversion; no device-change recovery / ASIO notification mechanism. These are separate product gaps. |
| Tests/build | CMake + all portable test executables + Windows scripts | Previously user ran 11/11 Windows CTests and registry-free tests on older source. New code adds two tests; expected Windows count now **13**. Portable `AsioBlockStampChannel` and double-buffer zeroing tests were built locally under clang ASan+UBSan and passed; Windows full MSVC compile and actual Cubase comparison **NOT run**. |

## Source revisions in this audit

1. `include/modern_asio/asio_block_stamp.hpp`: coherent lock-free sample-position/system-time pair, single writer and multiple readers. `getSamplePosition()` reads the same latched time for a frame instead of returning a new time for every call.
2. `src/asio_com_driver.cpp`: timestamp from `timeGetTime()` in the SDK-prescribed Windows time domain; pair published before the host callback, TimeInfo and getSamplePosition share it; a next-boundary pair is published after callback. **Remaining caveat:** this is host callback-time correlation, not rigorously calibrated IAudioClock physical device play time, and raw timeGetTime has an approximately 49.7-day wrap issue.
3. `tools/asio_host_smoke.cpp`: verifies stable pairs across repeated `getSamplePosition` calls inside a callback, and equality of timeInfo and getSamplePosition stamp when enabled; fails on pair discrepancy.
4. `include/modern_asio/asio_output_silence.hpp`: zero only the next output half before calling the host to eliminate previously left-over PCM if host does not write; do not zero pre-filled B when starting the device. `tests/asio_output_silence_tests.cpp` verifies isolation and no stale replay.
5. `tests/asio_block_stamp_tests.cpp`: 250,000 producer publishes plus concurrent reader pair consistency. Both new standalone tests compiled and passed with local Clang 17 ASan/UBSan. Expected total `ctest` cases after build: 13.

## What we can and cannot conclude

- Source proves 16 bars **cannot be physically queued as PCM in the existing SPSC FIFO**, and the tested deterministic producer policy does not reorder a known 16-bar silence/marker sequence. This narrows the fault to what Cubase places into ASIO callbacks, clock/transport mapping, or an *unreproduced* programming error.
- The two source defects addressed here are real correctness risks, but **the future-content symptom remains unverified after this patch**. Old logs lack bar-anchored input/output waveform fingerprints, so cannot show exactly which subsystem first produced the wrong musical passage.
- Static review cannot prove complete line/branch coverage, all Windows COM apartments and driver unload races, real 32/64-bit interoperability, physical latency, or Cubase multi-track correctness. The owner must build and test on Windows to close those gates.

## Next review/validation gate

1. Close Cubase before overwriting registered Debug DLL. `cd C:\ModernSharedASIO; git pull --ff-only; .\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug`. Expected 13 CTests. No new regsvr32 if registered path unchanged.
2. Manual mini-host should report `bad_sample_positions=0`, `bad_buffer_indices=0`, `inconsistent_stamp_pairs=0` with 440Hz tone and PASS. Main Cubase tests use a disposable copy of affected multitrack project only.
3. Record whether starting from the EMPTY region still plays the later ~16-bar material, and `%TEMP%\ModernSharedASIO-debug.log` last entry. These distinguish real fix from contract-only fix.
4. If future-content persists, do **not** make additional speculative buffer/latency changes. Implement bar-anchored, privacy-preserving PCM marker capture at the point Cubase fills ASIO buffers and at WASAPI render output (loopback or non-RT trace), allowing exact comparison; the driver does not directly know Cubase's playhead.

**Status: audited source; two static defects corrected and portable tests run; production Cubase bug UNRESOLVED until real runtime validation.**

### Critical new differential: live controller MIDI works, drawn MIDI does not (2026-10-18)

User found that playing VSTi from master keyboard gives responsive correct real-time sound, whereas the same project's programmed MIDI notes can trigger material apparently from ~16 bars later when transport is at an empty bar 0. This distinguishes real-time note triggering from sequencer playback and narrows investigation to Cubase's scheduled-VST processing, ASIO-Guard/preprocessing, latency compensation and host clock association. Existing 440Hz ASIO tests never exercised MIDI sequencing. See docs/CUBASE_LIVE_VS_SEQUENCED_MIDI.md. **No evidence yet whether record-enabling the same MIDI track while playing its programmed part changes the outcome.**
