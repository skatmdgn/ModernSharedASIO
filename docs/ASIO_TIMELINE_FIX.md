# Cubase 8.0.40 ASIO samplePosition compliance correction — 2026-10-18

## Root issue identified from official ASIO 2.3 specification

The official Steinberg/audiosdk ASIO header (common/asio.h) specifies `getSamplePosition()` sample position **zero for the first bufferSwitch after ASIOStart** and block-aligned positions thereafter. The original driver was incorrectly adding a pre-submitted 512-frame ASIO buffer B to the host-facing sample position. This explains why the prior isolated-host logs were exactly `callback_count * 512 + 512`: the arithmetic was self-consistent but violated the host contract.

Code now uses `AsioCallbackTimeline` to count **only callback blocks**, not host-prepared or Windows prefilled samples. On start / buffer creation position resets to zero. Prefilling output buffer B does not move the ASIO host position. First callback `bufferSwitch(index=0)` returns timeInfo/getSamplePosition frame 0, second index 1 returns 512, third index 0 returns 1024, and so on.

Important: this is a **confirmed contract bug**, NOT proof it fully caused Cubase/Serum failure. No new Windows build or Cubase rerun has happened yet.

Official SDK source: https://github.com/audiosdk/asio/blob/main/common/asio.h

## Regression coverage

- Portable `modern_asio_timeline_tests`: callback 0 starts at samplePosition 0 with a separate 512-frame output B prefill; 100,000 sequential 512-frame callback positions and stream reset tested.
- Local mirrored portable test built and passed with Clang 17 AddressSanitizer + UBSan, then optimized -O3 Release. The full Windows CTest suite needs another user build; expected now **10 tests** including this one.
- Registry-free `modern_asio_host_smoke.exe` now checks callback index alternation, verifies `IASIO::getSamplePosition()` **inside every callback**, checks optional TimeInfo callback position, asserts `first_callback_position=0` and `sample_position=callback_count*block_frames`. This test source is not yet compiled on Windows.

## Audio silence localization added

- `host_silent_blocks` counts callback blocks where neither active stereo output channel exceeds absolute 0.000001.
- `host_nonzero_left`, `host_nonzero_right` count blocks with audio above this threshold in each channel.
- `host_peak_left_ppm` / `host_peak_right_ppm` are maximum absolute ASIO callback sample magnitudes quantized as parts-per-million of full scale (one ppm = 0.000001 amplitude).
- `host_invalid_float_blocks` counts blocks containing NaN/Inf.
- These counters record data *immediately after Cubase's host callback, before the FIFO bridge*. They do not prove the samples actually reached the speaker.
- A read-only per-user `HKCU\Software\ModernSharedASIO\DisableTimeInfo` DWORD value `1` suppresses ASIO 2.x TimeInfo negotiation to allow testing the legacy bufferSwitch path; remove or change to 0 to restore. This is a temporary diagnostic compatibility switch, not a user-facing feature.

## Real user measurements from prior revision

User shared four streams at 48kHz, with requested period 5ms but actual `engine_period_frames=480` (10ms) and buffer 1056 frames. The first had 480 underrun frames, while other runs had zero. The host callback maxima were 876, 18567, 14724, 38 microseconds; only 1–2 over-budget calls in the longer runs. `last_reported_latency_frames=2080` is an uncalibrated 43.33ms *reported* output latency, NOT measured acoustic latency. The 5ms request did not result in a 5ms engine period on this device. Therefore earlier 'more CPU' or 'too many underruns' explanations were not justified by the data.

## Safe Windows A/B test

1. Save work and fully close Cubase first. Keep original Steinberg driver available. Run `cd C:\ModernSharedASIO; git pull --ff-only; .\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug`. Do not rebuild if Cubase holds the Debug DLL open.
2. Verify all **10 CTests** and the registry-free host smoke (standard and TimeInfo modes) with: `.\build-win\Debug\modern_asio_host_smoke.exe .\build-win\Debug\ModernSharedASIO.dll --tone --time-info`. The latter must now print `first_callback_position=0`, `bad_sample_positions=0`, `bad_buffer_indices=0`, `sample_position_low = callbacks*512` and PASS. Test normal `--tone` too if possible.
3. DLL is at the **same registered path**, so do **not** run the registration script again. Launch Cubase; empty 48kHz project, first audio-only, then Serum INIT preset alone. Use the same known-working MIDI input/pattern for comparisons.
4. Fully close Cubase, then `Get-Content "$env:TEMP\ModernSharedASIO-debug.log" -Tail 5`. Compare separate sessions: an audio-track session versus Serum-only session.
5. If Serum remains silent, the log indicates whether Cubase supplied nonzero ASIO samples. If host_nonzero_left/right = 0 for entire Serum-only run, data is absent **before the driver FIFO**; investigate Cubase host scheduling/timing and routing, not the Windows PCM conversion first. If output peaks are nonzero and underrun_frames=0 but nothing is heard, trace FIFO/endpoint handling.
6. Optional ONLY if still broken: compare TimeInfo mode using ordinary PowerShell while Cubase is closed: `New-Item -Path HKCU:\Software\ModernSharedASIO -Force | Out-Null; New-ItemProperty -Path HKCU:\Software\ModernSharedASIO -Name DisableTimeInfo -PropertyType DWord -Value 1 -Force | Out-Null`; restart Cubase and repeat. Then restore using `Remove-ItemProperty -Path HKCU:\Software\ModernSharedASIO -Name DisableTimeInfo -ErrorAction SilentlyContinue`. Do not change this while Cubase is running; ASIO callback mode is negotiated in createBuffers.

## Status

Source-written + portable isolated test passed; Windows compile and Cubase verification pending. Root bug corrected at source level; actual Serum usability NOT YET VERIFIED. Do not interpret fixed sample positions as proven latency/quality improvement. Existing proprietary `asioglld.dll` and ASIOGLLD reference untouched. CI remains deferred to final release gate.
