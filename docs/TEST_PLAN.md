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

47. Fresh Cubase project at 120 BPM 4/4: Audio plays approximately 24 bars ahead (48s), programmed MIDI notes fail entirely, live controller MIDI VST works. Prior 16 bars=32s. Eliminate 'only audio works' as older report; new clean project contradicts it.
48. Diagnostic timing discriminant: starting from same zero position after 30s Cubase transport STOP while ASIO driver/engine loaded—if forward audio offset increases by ~15 bars, look for uncorrected ASIO engine uptime entering project timeline; if not, investigate other transport/PDC/ASIO SDK sync. Leave tempo, routing, tracks unchanged. No new DLL code before this test.

49. New 100-callback ASIO handoff regression: prefill B with .25, callback 0 fills A with 1 and must output B .25; callback 1 fills B with 2 must output A 1; callback 2 fills A with 3 must output B 2. Continue for 100 calls, compare every output sample; this fails previous immediate-current-half driver policy.
50. Windows CTest expected 14. With normal Cubase master-keyboard / Audio / drawn MIDI use, ensure stable live input AND restoration of sequenced playback + no future 24-bar content. No proof of cure until actual comparison.

51. 2026-10-18 Cubase 8.0.40 on-user-PC acceptance: **user reports PASS** after double-buffer handoff commit d3d770b (“now everything plays correctly”). This closes the user-reported fresh-project Audio/MIDI regression for their environment, but no individual itemized validation output or Windows 14-CTest transcript was provided. Continue testing stop/start/seek, multiple plugin tracks, long duration, audio system changes and ASIO latency accuracy before any release classification.

52. Optimization phase 1 baseline protection: `stable/cubase8-verified-20261018` at user-verified `19f79a9`. Keep default ASIO 512, TimeInfo disabled, producer queue target two blocks, WASAPI 5ms requested/10ms observed, and alternate ASIO output-half handoff. New direct SPSC planar writer **must** remain PCM-identical and no host callback contract changes.
53. Run `modern_asio_planar_fifo_tests`: 8 capacities, 120,000 randomized old/new stereo/mono/null/partial/wrap equivalence operations and 250,000 frames of true 2-thread SPSC identity. Run under ASan/UBSan outside Windows and normal MSVC CTest.
54. Run `modern_asio_queue_target_tests` 441/480-period, 512/1024-target 200k-cycle deterministic model; this does NOT test Windows scheduling/underruns. With QueueTargetBlocks absent expect 1024-frame target and known-good Cubase MIDI/Audio. Then **opt-in** QueueTargetBlocks=1, repeat separately and check clicks/dropouts/VST real-time stress and ASIO_STOP target=512.
55. Expected Windows total **16** tests; bridge microbenchmark only via MSA_BUILD_BENCHMARKS=ON and excluded from CTest. Do not claim 16/16 or real-world latency gain until user sends results. CI final only.

56. Optimization Phase 2: direct WASAPI Float32 x2 path must exactly match prior FIFO.pop -> scratch -> memcpy output on wrap, underflow, empty queue, and different WASAPI-vs-FIFO capacities. New modern_asio_direct_output_tests uses 60,000 random differential cases and 1152>512 chunk test; Linux Clang ASan/UBSan PASS. Windows CTest expected 17/17, not yet measured.
57. In real Cubase sessions check native_direct_frames>0 when native Windows mix format is stereo Float32, otherwise expect safe staged fallback. ASIO_STOP must still report zero glitches/callback errors, correct timeline on Audio and MIDI, no VST dropouts. Prior ASIO opposite-half invariant unchanged. Avoid claiming a 1.425x overall gain from a 0.1us/call local memory benchmark.

60. User's 48.635s real Cubase native-direct low-queue session was audibly fine but logged 2976 missing frames, 4 callbacks over budget, max 30371us, 100% direct output. Always verify ASIO_STOP underrun_frames, not subjective sound alone.
61. One-variable safety test: remove HKCU QueueTargetBlocks=1 while Cubase CLOSED, restart identical project, expect audio_buffer_target_frames=1024, verify no Audio/MIDI timeline regression and underrun_frames=0 under comparable VST load (60s+). Do NOT change 256-frame ASIO experimental branch or TimeInfo simultaneously.
62. Newly added `modern_asio_underrun_event_tests` checks per-render event counting, max missing frames, first event and reset with concurrent reader. Expected Windows CTest 18 after rebuild, not yet proven. New ASIO_STOP fields: underrun_events, largest_underrun_event_frames, first_underrun_render_event. Those cannot retroactively reconstruct old 2976 missing frames.

63. User follow-up accepted **queue target 1024 at 48k/512**: 76.043s real music session 7127 ASIO callbacks, 6523 nonzero, 0 underrun frames, 0 stream/callback errors, 3 callback overruns with max 18608us. Direct native frames equal submitted 3648576. Compare prior 512 target 2976 underrun (max callback 30371us); workload unmatched so not a controlled causal proof. Keep default two ASIO blocks recommended for now.
64. User logs lack newly implemented `underrun_events` fields; do not count them as execution or Windows verification of the 18th CTest/latest diagnostics. Repeat same load with new instrumented DLL at default setting only after user is ready. Measure actual physical latency before proposing lower-buffer mode; avoid simultaneous configuration changes.

65. Phase-3 portable modern_asio_realtime_load_tests: verify callback duration histogram buckets, maximum over-budget streak/first block, queue min/max and concurrent atomic snapshot; Clang ASan/UBSan and GCC Release PASS.
66. Expect 19 CTests on Windows; compare real Cubase with ASIO block=512, queue target=1024, TimeInfo disabled. Parse cb_time_hist_500_1k_2k_4k_8k_12k_20k_us and render_queue_below_demand_events (queue pressure only; underrun_frames is authoritative).
67. Do not infer less audio latency from the phase-3 telemetry or change default buffers on this commit. Preserve A/B opposite-half output rule and verified phase-2 stable branch.

68. Check if 480-frame missing PCM is restricted to first render event. All-silent 1.509s run had exact event1 480 missing frames, all 138 host callbacks <=500us, no DSP over budget. Longer 14.299s run zero underrun. Treat as startup producer-wake/device-start race candidate, not VST load proof.
69. Experiment-only EarlyProducerWake DWORD=1 requests producer before render_.start; default OFF; rollback remove key and restart Cubase. Keep ASIO 512 and FIFO 1024. Compare repeated starts and exact first event underruns, plus full audio/MIDI correctness. Deterministic new early_prefill_handoff_tests tests opposite-half PCM and 480-frame FIFO order; Win32 scheduling unverified.


70. First Win/Cubase report includes 3 log lines but only 2 with explicit EarlyProducerWake=1; 14.659s older-format row cannot verify ON. Neither explicit ON run had an event1 underrun. Loaded 48.420-second session showed 480 missing frames over 2 events, max single deficit 384 frames, min FIFO 96 frames, first underrun render event276, 4 ASIO callbacks over budget, maximum 23072us. Do NOT confuse with startup-only underrun; require equivalent OFF/ON repeated runs to infer change. No real validation of sustained 0-underrun mode.
71. Track separate latency contract issue: getLatencies() currently returns provisional 2*kFrames+render buffer_frames (1024 before WASAPI init, 2080 after 1056-frame device buffer available). This can yield different reported latencies per invocation. Review without changing optimized PCM or host timing arbitrarily; measure physical/audio-client latency first.
