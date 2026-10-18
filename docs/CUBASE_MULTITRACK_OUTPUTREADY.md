# Cubase 8.0.40 multi-track stale/future audio — ASIOOutputReady contract fix

## User's decisive symptom (2026-10-18)

- A single track plays normally; more tracks produce increasing delay, stuttering VST instruments, or Serum silence.
- Starting Cubase playback at bar 0 may output audio belonging around **bar 16** (user estimate). The exact project tempo/time signature and source identity have not been measured.
- On an earlier Debug build, recorded WASAPI underrun_frames=0 for long streams even while Cubase VST audio failed; actual Windows shared period stayed 480 frames (10ms) despite a requested 5ms.

## Confirmed ASIO API contract bug found in source

- The COM driver implemented `IASIO::outputReady()` by **unconditionally returning `ASE_OK`**, but it does not implement an asynchronous host `ASIOOutputReady` completion protocol. It immediately reads/copies host-owned ASIO output buffers right after `bufferSwitch` / `bufferSwitchTimeInfo` returns.
- The official `audiosdk/asio` ASIO 2.3 `common/asio.h` explicitly says to return **`ASE_NotPresent`** if the driver cannot take advantage of `ASIOOutputReady` notification. Advertising `ASE_OK` informs the host that the driver supports the optimization, so hosts may process asynchronously and signal readiness afterward. Our code ignored that signal.
- With a simple synchronous one-track host, this mismatch may not surface. A multitrack Cubase processing graph with ASIO-Guard/deferred paths could expose incompletely written or stale audio blocks. This is a plausible mechanism, **not yet proved to be the cause of the ~16 bar offset**.
- Source now returns `ASE_NotPresent` and neither claims the optional optimization nor promises a handoff it cannot honor.
- `modern_asio_preflight` and the separate registry-free mini ASIO host now explicitly call `outputReady()` and fail if it returns anything but `ASE_NotPresent`, preventing recurrence.

Official contract: https://github.com/audiosdk/asio/blob/main/common/asio.h (ASIOOutputReady declaration and Returns paragraph).

## Why this is not ordinary output-buffer latency

- At 48kHz a fixed ASIO block is 512 frames (~10.67ms), the producer target is 1024 frames (~21.33ms), and the last observed shared device buffer was 1056 frames (~22ms). These cannot themselves hold a whole 16-bar segment of PCM.
- Track-dependent *musical content offset* also implicates the Cubase render graph, timing / ASIO-Guard / plugin delay compensation or misuse of the ASIO outputReady contract, not merely audio queue depth.

## Next user tests, in order

1. Save and close Cubase completely. Build updated DLL with `git pull --ff-only` followed by `scripts/build_windows_x64.ps1 -AsioSdkDir C:\sdk\asio -Configuration Debug`. Expect **10/10 CTests** including COM preflight with `outputReady=-1000` (SDK ASE_NotPresent). Never modify the original Steinberg DLL.
2. Run manual registry-free TimeInfo mini-host: `.\build-win\Debug\modern_asio_host_smoke.exe .\build-win\Debug\ModernSharedASIO.dll --tone --time-info`. Expect a line confirming `ASIOOutputReady capability=-1000`, first callback position 0, bad_sample_positions=0 and PASS.
3. Provided Windows registry still points to the same `build-win\Debug\ModernSharedASIO.dll` and Cubase was closed, a rebuild at the same DLL path does not require re-registration. If Cubase driver is not listed, inspect registry entries before re-registering; do not blindly delete or replace registry keys.
4. Open a COPY of the affected Cubase 8.0.40 project, keep tempo/transport identical. Test 1 track, then the track count where displacement begins, then a Serum-containing group. Measure: playhead at bar 0; which bar's source audio is heard; does track count change the offset?
5. If still broken, **test ASIO-Guard OFF separately** in Cubase 8: Devices > Device Setup > VST Audio System > deactivate ASIO-Guard. Apply, restart Cubase if needed. This is a targeted test because the Cubase engine can pre-process extra tracks. Restore previous setting after.
6. If still broken, separately test the existing read-only per-user TimeInfo diagnostic switch (Cubase CLOSED): `New-Item -Path HKCU:\Software\ModernSharedASIO -Force | Out-Null; New-ItemProperty -Path HKCU:\Software\ModernSharedASIO -Name DisableTimeInfo -PropertyType DWord -Value 1 -Force | Out-Null`. Restart Cubase and check. Restore with `Remove-ItemProperty -Path HKCU:\Software\ModernSharedASIO -Name DisableTimeInfo -ErrorAction SilentlyContinue`.
7. Capture `%TEMP%\ModernSharedASIO-debug.log` last entries after Cubase closes, especially `host_silent_blocks`, `host_nonzero_left/right`, `host_peak_left/right_ppm`, `underrun_frames`, `asio_time_info` and callback count.

## Status

CODE_FIX_WRITTEN=true; Windows build and tests after this fix NOT RUN; Cubase multi-track outcome UNKNOWN. Do not promise that incorrect outputReady is the sole root cause. Do not introduce arbitrary deep buffering or buffer-size changes pending evidence. The old Steinberg registration and DLL must remain intact. CI remains deferred.
