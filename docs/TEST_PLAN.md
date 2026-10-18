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
