# Cubase 8 future-bar audio while transport is at zero — isolation revision

Date: 2026-10-18. User's exact observation: the start of the Cubase timeline contains NO audio clips but transport playback from bar 0 outputs music that belongs roughly 16 bars later. With one track playback may be normal; increasing the number of tracks makes the time displacement and VST errors worse. This is a **future-source-content mismatch**, NOT an ordinary 16-bar output buffer or playback latency.

## Evidence and defensible interpretation

- User's previous three ASIO_STOP sessions used the 48kHz/512-frame driver and zero WASAPI underruns/stream errors; one session was 6417/6417 all-silent host blocks, another had 1853 nonzero host blocks, third had 5/5 all-silent. These totals are whole ASIO sessions, which may include stopped Cubase transport, so they do NOT map to bar positions.
- A standalone tone generator passed 440Hz tests but never exercises Cubase seeking, complex mixing, ASIO-Guard, plug-in delay compensation, or an initial 16-bar silent musical interval. This is an important testing gap.
- This repository uses a ring queue with a 1024-frame producer target, a separate initial 512-frame B-buffer pre-roll, and the user's measured WASAPI buffer size 1056 frames. At 48kHz these add at most tens of milliseconds under the intended policy, not tens of seconds. A 16-bar-ahead recording in bar 0 would need the **source signal** to be wrong when Cubase calls the ASIO host callback, or a much more severe logic defect not yet reproduced.
- The ASIO driver currently declares kSystemTimeValid in ASIOTimeInfo, but uses callback-time QPC nanoseconds instead of correlating the timestamp to the physical device output frame clock. This time/position relationship has NOT been externally validated for Cubase. No proof that it causes the bar offset yet.

## Changes after earlier unsuccessful fixes

1. Default ASIO callback mode changed to **legacy bufferSwitch** (no ASIOTimeInfo declaration) until the time/position association is calibrated. The ASIO host can still explicitly opt into the experimental TimeInfo path by setting HKCU\Software\ModernSharedASIO\EnableTimeInfo DWORD=1. The older DisableTimeInfo DWORD=1 continues to override. No new registry writes are made by the DLL.
2. The registry-free ASIO host smoke now records what callback mode actually ran (legacy vs TimeInfo) independently of what the test requested. The ASIO callback position assertions remain enforced. `--time-info` now prints a requested mode, not a claim about the driver's negotiated mode.
3. New `modern_asio_content_order_tests` models **16 bars of exact silence followed by a marker at the start of bar 17**, with a 512-frame prefilling block, 512-frame producer blocks, 480-frame consumer periods, and the actual production fill policy and ring/ASIO bridge. It requires the first output marker at 1,536,512 frames, not at playback start, and treats ANY unexpected ring underrun/order corruption as a test failure.
4. Added stop-time metrics: `asiodriver_wall_ms`, `asio_callback_audio_ms`, `wasapi_submitted_frames`, `remaining_fifo_frames`, `first_nonzero_block`, `last_nonzero_block`. These can show if the driver invokes host DSP too quickly compared to real wall time and when nonzero ASIO samples began in a session. They still do not reveal Cubase bar positions or whether the transport was playing; do NOT interpret first_nonzero_block as an absolute project bar.

## Verification

- The **same producer FIFO policy and PCM marker sequence** was independently mirrored in a small JavaScript model; at 48kHz the first output signal was frame 1,536,512 (16 bars of 120BPM/4-4 silence plus 512-frame pre-roll), no underruns and max queue depth 1504 frames. This is a policy/sequence model, not a compile or real Cubase validation.
- New C++ regression test source and default TimeInfo change have been committed, but **Windows compilation, full CTest, and actual Cubase behavior are NOT YET VERIFIED**. Expect 11/11 CTests if Windows build succeeds. Current original Steinberg DLL/registration remains unchanged.
- This source change is a conservative isolation candidate. It is NOT established as the definitive fix for 16-bars-late source output.

## Next hard gate

Close Cubase before rebuilding the registered Debug DLL; with the same registered path no separate regsvr32 action is necessary. Run `git pull --ff-only` and `scripts/build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug`. Check 11 CTests. In Cubase 8.0.40 use a COPY of a multi-track project, transport bar 0 with the same known-empty section and the later known audio region, then report whether future-region audio still leaks into bar 0. After closing Cubase, `Get-Content "$env:TEMP\ModernSharedASIO-debug.log" -Tail 2`: `asio_time_info` should now be `0` by default. If the displacement remains, stop making unverified driver guesses; next task is instrumenting Cubase host callback PCM fingerprints versus project marker positions and seeking/reset event boundaries; client-side transport state itself is not exposed to this driver.

Safety: for valuable Cubase projects, switch back to the known-working Steinberg ASIO driver. Do not overwrite proprietary asioglld.dll. CI deferred until final.
