# Cubase 8.0.40 VSTi timing diagnosis — 2026-10-18

## Actual Windows observations

- Cubase recognizes and loads the new ASIO driver. Audio waveform tracks play, but the user reports severe delay, stuttering VST instruments, and Serum silence.
- The prior five-second 440Hz standalone ASIO host passed in both bufferSwitch and bufferSwitchTimeInfo modes, but does not test Cubase's actual plugin DSP cost.
- This is an **unsolved** Cubase compatibility/real-time audio defect. Do not use the experimental DLL in important projects.

## Code-audited risk factors

- At 48kHz ASIO uses fixed 512-frame blocks (~10.67ms). Previously the Windows shared engine used its default 480-frame period (10ms) with a measured 1056-frame output buffer (22ms). Target SPSC producer queue depth was 1024 frames (~21.33ms). These values are not directly additive into a proven physical latency.
- WASAPI consumes available FIFO data and only afterward signals the ASIO DSP producer. An expensive VST callback could arrive too late; the exact cause of Serum silence is not proved.
- getLatencies currently returns an uncalibrated value, which may differ before and after renderer start. Output delay may also involve sound device and Cubase configuration.

## Experimental code changes — not yet built on user Windows

- The Cubase driver requests a 5ms WASAPI shared-mode period by default (previously 0=Windows default). IAudioClient3 clamps to supported engine quanta; if this initialization fails, the existing code falls back to conventional shared IAudioClient initialization. 5ms is a **candidate, not a validated improvement**.
- Per-user HKCU\Software\ModernSharedASIO PeriodMs REG_DWORD override: 0=Windows default, 3/5/10=request target milliseconds; missing/invalid defaults to 5. This is read-only inside the DLL and does not modify the system's audio settings.
- QPC timing around each Cubase ASIO host callback records total callback count, longest callback in microseconds, and how many callbacks exceeded one ASIO block duration. WASAPI underrun frame count, engine period, render events and HRESULT are also tracked.
- A bounded diagnostic line is appended when Cubase calls IASIO::stop(), after audio workers have joined, to %TEMP%\ModernSharedASIO-debug.log. No disk writes are performed in audio callbacks. If Cubase crashes or hangs before stop, the line may be absent.
- Note: a slow host callback is not guaranteed to be the only reason for a WASAPI underrun; these metrics are complementary.

## Safe reproducible Windows tests

1. Save projects and CLOSE Cubase completely. The currently registered Debug DLL path is fixed, so do not try to overwrite it while Cubase is holding it open.
2. Ordinary PowerShell: cd C:\ModernSharedASIO; git pull --ff-only; .\scripts\build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug. Verify 9/9 automatic tests again.
3. Because the registered COM InprocServer32 path remains C:\ModernSharedASIO\build-win\Debug\ModernSharedASIO.dll, rebuilding that same location while Cubase is closed does not require another regsvr32 invocation.
4. Use an EMPTY Cubase 8.0.40 project at 48kHz. Try a basic audio file, one lightweight built-in VSTi, then Serum with an INIT preset. Play YouTube simultaneously only after the basic playback test.
5. Stop playback, switch away from Modern Shared Audio in Cubase or close Cubase entirely to trigger IASIO::stop. Then run: Get-Content "$env:TEMP\ModernSharedASIO-debug.log" -Tail 5
6. For an A/B check against the old engine period, run: New-Item -Path HKCU:\Software\ModernSharedASIO -Force | Out-Null; New-ItemProperty -Path HKCU:\Software\ModernSharedASIO -Name PeriodMs -PropertyType DWord -Value 0 -Force | Out-Null. Restart Cubase fully and repeat the identical project.
7. To restore the experimental 5ms request: Set-ItemProperty -Path HKCU:\Software\ModernSharedASIO -Name PeriodMs -Value 5
8. Record the auditory result and both log lines. If severe glitches/crash persist, switch back to the original Steinberg driver. Do not infer improvement from smaller period alone.

## Verification gate

New code: SOURCE_WRITTEN; Windows MSVC build: NOT YET TESTED; Cubase 5ms runtime: NOT YET TESTED; original versus new driver A/B latency: NOT TESTED. No CI, no other features, original DLL unchanged.

## Additional multi-track failure and corrected outputReady declaration

User reports 1-track normal playback but multi-track increasing delay, VST dropout and audio offset ~16 bars even when starting playhead at 0. Official ASIO SDK states that `ASIOOutputReady` should return ASE_OK only if the driver supports the optional host-finished-output handshake. This driver's outputReady returned ASE_OK without using the notification: a concrete false advertisement potentially unsafe for Cubase's asynchronous multitrack processing. Switched to ASE_NotPresent and added preflight and mini-host checks. Windows build and multi-track Cubase tests pending. See docs/CUBASE_MULTITRACK_OUTPUTREADY.md.
