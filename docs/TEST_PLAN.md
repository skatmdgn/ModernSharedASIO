# Windows validation (NOT_YET_RUN)

1. Visual Studio 2022 + Windows SDK x64 Debug/Release build; run CTest for both. Never rely on debug-only `assert` tests for Release.
2. `modern_asio_smoke.exe` default: confirm mix rate/channel/buffer, IAudioClient3 selection, event and underflow counts; concurrently play a YouTube video. Silence from the smoke app is expected.
3. `--tone` at low listening volume: check both 440Hz -36 dBFS tone and browser audio can play.
4. Repeat `--period-3ms` and `--period-5ms`; log whether the engine accepts the period or shared fallback is used.
5. Inspect device formats mono, stereo, multichannel, 16/24/32 PCM and float32; assert conversions and channel layout.
6. Change/disconnect default audio device midstream: expect noncrashing error and clean shutdown. Automatic re-open not implemented.
7. Use ETW/WPR to record scheduling, CPU, glitches, missed deadlines, underruns. Compare against original only with identical device/settings.
8. Future ASIO DLL: Cubase 8.0.40 can enumerate unique driver, init, create/dispose buffers, query sample rate/latency, play while YouTube plays, reset/restart and close safely.
9. Soak >= 30min and compare original/new underruns, CPU and measured loopback latency before performance claims.

Any unexecuted item stays NOT_TESTED rather than PASS.

10. Run `modern_asio_clock_sim_tests` to validate frame conservation for 44.1kHz and 48kHz *simulated* 10ms events. These results are never proof of actual Windows performance.
11. Log IAudioClock device position, GetFrequency and QPC (100-ns units) from smoke output. Correlate against actual render events and underruns in Windows before claiming drift/latency improvements.
12. Check that the ASIO callback directly on the WASAPI render worker does not block the device deadline on heavy Cubase projects. If it does, implement a bounded, clock-paced producer rather than reinstate a free-running timer.

13. Validate startup callback isolation; first priming never invokes Cubase producer. A later real WASAPI device event may race outer ASIO start return and needs host validation.
14. Record producer_failures separately from stream_errors and underrun_frames; startup zero padding is intentional, not a real runtime xrun.
15. Use scripts/build_windows_x64.ps1 for Windows x64 build and registry-free COM preflight, never automatic driver registration.

16. Batch5 producer policy test (`modern_asio_producer_policy_tests`): 60,000 randomized capacity/target/producer block cases; no overflow or unbounded production. Run on Windows too.
17. Measure Windows producer callback duration and event→producer wake latency. Compare queue starvation and added 1024-frame target buffering to Batch4; do not assume lower latency than the old synchronous path.
18. Verify no Cubase host callbacks occur on the WASAPI worker and that stop/join safely handles host callback timeouts/reentrancy. Run device unplug and teardown torture before registering the DLL.
19. Run the Batch4 startup-isolation test, now added to CMake (it was present as source but not registered in the previous CTest list).

20. New `modern_asio_clock_observation_tests`: verify coherent snapshot reads during 400,000 publications and 100,000 randomized clock conversion comparisons against exact 128-bit reference arithmetic (the reference loop is excluded on MSVC).
21. Windows smoke: capture `coherent_played_frames`, `submitted_frames` and paired `qpc_ns`. Compare only valid S_OK IAudioClock readings; S_FALSE denotes degraded position precision.
22. Verify `last_stream_hresult` on device removal, render failure and no-event timeout; don't assume every `producer_failures` counter means a Cubase DSP callback failed (in asynchronous mode it may indicate inability to signal the producer).
23. Never use device frame offset directly as ASIO buffer samplePosition without establishing pre-roll/silence and output latency alignment; the current ASIO sample position is generated-frame-based and not calibrated to played frames.

24. Windows WASAPI smoke 440Hz and simultaneous YouTube: **user reports audible output, IAudioClient3=1, errors=0; underrun_frames=960**. Investigate whether missing frames are startup/scheduler artifacts, not automatically audible Cubase failures.
25. Manually run registry-free `modern_asio_host_smoke.exe <DLL> --tone` after recompiling. Verify IASIO::init, getChannels, getSampleRate, getBufferSize, createBuffers, start, bufferSwitch callbacks, stop, getSamplePosition, disposeBuffers. The program does not register the driver and is NOT an automatic CTest.
26. Repeat ASIO host smoke with `--tone --time-info` for ASIO 2.x TimeInfo path. Record callback_count and error code; test YouTube concurrently. Use moderate speaker volume and close Cubase during tests.

27. User successfully played registry-free ASIO stereo 440Hz using `bufferSwitch` mode with simultaneous YouTube: 472 callbacks, invalid_buffers=0, sample_position_low=242176 = 472*512+512. PASS. Repeat in ASIO TimeInfo mode prior to any Cubase registration; do not treat ASIO TimeInfo/Cubase as already validated.

28. User confirmed registry-free ASIO 2.x bufferSwitchTimeInfo mode: 471 callbacks, invalid_buffers=0, sample_position_low=241664 = 471*512+512 initial preroll; audible concurrent YouTube. Verified standalone ASIO host callback path, not Cubase.
29. Next Cubase gate: controlled independent 64-bit registration (use scripts/register_cubase_test.ps1), optional rollback (scripts/unregister_cubase_test.ps1). Start with EMPTY 48kHz project, verify Stereo Out 1/2 connection, compare simultaneous YouTube. Review registration before running; no existing Steinberg DLL replacement or CI yet.

30. Reported actual Cubase 8 audio file playback works but VSTi stutters/Serum silence and high latency. Prior standalone mock ASIO tests are insufficient: quality gate FAILED.
31. Rebuild new 5ms period diagnostic candidate and inspect stop-time %TEMP%\ModernSharedASIO-debug.log. Compare callback_over_block_budget, max_host_callback_us, underrun_frames, stream_errors, and effective engine_period_frames.
32. Compare PeriodMs=5 versus PeriodMs=0 in otherwise identical Cubase 48kHz empty project, record audible differences and diagnostic lines before further algorithm changes. No register/unregister needed for an unchanged COM DLL path. Distinguish plugin processing delay from driver underrun; exact cause unverified.

33. NEW ASIO 2.3 compliance fix: initial output B preroll does NOT advance reported ASIO sample position. First callback position=0, subsequent callback positions=512,1024,... . Host mini-test asserts no mismatch for timeInfo and non-timeInfo callbacks. Portable independent timeline test locally Clang ASan/UBSan and Release PASS (100000 blocks).
34. After Windows rebuild/10 CTests, mini-host output must report `first_callback_position=0`, `bad_sample_positions=0`, `bad_buffer_indices=0` and `sample_position_low=callbacks*512` (not plus 512). Full Cubase/Serum playback must still be tested; no claims of cure.
35. For Serum silent but 0 underruns, compare an audio-file-only test versus Serum-only Cubase session using `host_silent_blocks`, `host_nonzero_left/right`, `host_peak_left/right_ppm`, `host_invalid_float_blocks`, and `underrun_frames`. If unchanged, compare TimeInfo mode to legacy mode with HKCU `DisableTimeInfo=1`, restore after.

36. Multi-track real Cubase failure: 1-track normal, increasing track count appears to play a later (~16bar) passage while transport is at bar 0. ASIO FIFO buffer sizes cannot explain a 16-bar memory shift. Verify API handling and Cubase ASIO-Guard / delay compensation.
37. ASIOOutputReady contract: driver lacked readiness handshake but returned ASE_OK (incorrect). Now ASE_NotPresent. New registry-free COM preflight and ASIO host MUST see outputReady=-1000. Windows rebuild + 10 CTests before Cubase; do not register again if existing COM path unchanged.
38. Repeat identical Cubase project and track counts. If still broken, ASIO-Guard OFF A/B separately, then HKCU DisableTimeInfo=1 A/B separately. Collect stop-time logs, audio source bar vs Cubase transport bar, and restore settings. No unsupported causality claims.

39. Forward-content test: 16 bars silent at 120BPM / 4-4, marker begins at frame 1536000. Following 512-frame ASIO pre-roll, output first marker must be frame 1536512. Any sample mismatch, early marker, underrun or reorder fails C++ test `modern_asio_content_order_tests`.
40. Legacy bufferSwitch mode now default while ASIO TimeInfo device-clock correlation is uncalibrated. Mini host records legacy and timeInfo callback counters independently, and Cubase stop log must show asio_time_info=0 by default; explicit experimental EnableTimeInfo=1 opt-in, older DisableTimeInfo=1 overrides.
41. Live multi-track Cubase check must specifically test whether source at bar 16 plays while transport and earlier audio clips are empty at bar 0. Collect wall-vs-audio time and first_nonzero_block values, but do not mistake whole-session host silence counts for transport-location evidence. Actual Cubase result pending; production quality gate FAILED.

42. Full static source audit (see docs/FULL_SOURCE_AUDIT_2026_10_18.md): ASIO block-position/timeStamp must be one stable pair inside each bufferSwitch; TimeInfo stamp must exactly match getSamplePosition pair. `modern_asio_block_stamp_tests` concurrently publishes 250k positions with coherent paired times; Clang ASan+UBSan local PASS.
43. Reused ASIO output planes must be zeroed before callback (but preserve B-half prefill at start). `modern_asio_output_silence_tests` verifies old signal cannot survive into silent callback; Clang ASan+UBSan local PASS.
44. Windows CTest expected 13 after recompilation. Registry-free host must show `inconsistent_stamp_pairs=0`, `bad_sample_positions=0`, `bad_buffer_indices=0`. Cubase future-content at bar0 still unresolved until runtime test. Do NOT claim fixes cure Cubase; if persistent build source+WASAPI correlated marker tracer.

45. Critical Cubase MIDI processing discriminator: physical master-keyboard performance live correct, drawn timeline MIDI note playback wrong/out-of-position. On SAME VSTi track containing SAME MIDI region, compare Record Enable OFF vs ON (transport normal PLAY, not recording), without changing ASIO driver or project data. Report whether future-bar MIDI audible error changes. If not, prioritize transport/sample-clock and sequencing compensation rather than only ASIO-Guard.
46. Any diagnostic tool must distinguish ASIO output audio block waveform from source Cubase MIDI note positions; driver cannot inspect Cubase MIDI scheduling through IASIO itself. Previous mini-host tone tests do not prove such scheduling.
