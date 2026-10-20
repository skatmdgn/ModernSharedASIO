# Live WASAPI output routing from Cubase 8 ASIO Control Panel

2026-10-20 | `feature/output-device-picker` experimental branch, not merged

## Why the previous UI did not change where sound played

Originally `IASIO::controlPanel()` only called `show_output_device_picker()`, which persisted `HKCU\Software\ModernSharedASIO\OutputDeviceId`. A running WASAPI stream held a previously opened `IAudioClient` on the old endpoint and never consumed updates. On top of this, a newly compiled DLL is **not automatically installed**; Cubase may be running a different registered DLL, making even restart-only tests misleading.

## New Cubase embedded-panel behavior

After pressing **Save** in the Cubase ASIO Control Panel:

1. The control thread resolves the saved selection through the normal endpoint policy (including an offline-headset fallback to Windows default). It queries the actual output's Windows mix **sample rate**, without opening a playback stream.
2. If the actual endpoint matches the current one, leave the stream untouched.
3. If the endpoint changes but the rate **equals Cubase's negotiated ASIO rate**, keep Cubase's ASIO DSP producer running, stop/join only the WASAPI render worker, lock the newly selected endpoint ID, and start a new WASAPI shared stream. This switches actual speaker/headset routing **without quitting Cubase**. A brief mute/transition is possible: this is NOT a sample-perfect gapless switch.
4. If the new endpoint has a **different sample rate** (e.g. Cubase 48k, headset 44.1k), leave current playback unchanged and show a restart-required warning. It would be incorrect to feed the old ASIO sample rate into the new physical clock without resampling or host renegotiation.
5. If the newly selected output cannot start, attempt to restore the original endpoint; show a warning if the restore works or if both endpoints fail. No new stream consumes FIFO until the previous render thread has joined.
6. If Cubase's ASIO driver is initialized but not currently playing, update the effective endpoint for the next `start()` without launching a stream; unlike earlier code, no full Cubase restart is required at the same sample rate.
7. The ASIO 512-frame host callback, opposite-buffer-half playback ordering, FIFO 1024-frame default, stream format conversions, output timestamp channel and core callback timeline **remain unchanged**. No ASIO driver registration or Windows default output settings are changed.

**Scope limitation:** Live routing applies when the **Cubase-embedded** ASIO Control Panel Save is used. Standalone `modern_asio_output_devices.exe` writes user preference only and **cannot message Cubase's already-loaded IASIO instance**; quit/reopen Cubase after using the standalone tool. A physical headset plug/unplug alone does NOT automatically cause a running stream to migrate yet. Startup fallback on reconnect remains supported. For a true hardware-notification auto-hotplug implementation, `IMMNotificationClient` and a deferred control-thread migration state machine are still needed; do not present as complete.

## New logs (only when Cubase is using this DLL)

`ASIO_STOP output_device_switches=N output_device_switch_failures=M`:
- `output_device_switches`: successful running-stream route migrations since last `start()`.
- `output_device_switch_failures`: route-inspection, rate mismatch or renderer reopen failures since last `start()`. A saved preference may have changed despite an unsuccessful live application.
- Existing `output_device_fallback=1` continues to mean the preferred device was missing during endpoint resolution.

Read `scripts/summarize_asio_stops.ps1 -Last 20` for `DeviceSwitches` and `DeviceSwitchErrors`. No raw user device IDs are written in ASIO_STOP.

## Deployment and validation

**Before attributing silence to code:** check the actual loaded/registered binary:

```powershell
cd C:\ModernSharedASIO
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\diagnose_output_route.ps1" -Configuration Debug
& ".\build-win\Debug\modern_asio_output_route_probe.exe"
```

Build the latest experimental branch only after Cubase is closed:

```powershell
cd C:\ModernSharedASIO
git fetch origin
git switch feature/output-device-picker
git pull --ff-only
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\build_windows_x64.ps1" -AsioSdkDir "C:\sdk\asio" -Configuration Debug
```

**Build does not register/install the DLL.** Do not assume Cubase loads it until diagnostics show a matching registered path/hash and, ideally, process-module path. Never overwrite the original Steinberg `asioglld.dll` or unrelated ASIO registry entries.

After verifying the exact DLL, test a single Cubase project at 48kHz with speakers and headset both set to a matching 48kHz Windows shared-mode format. While Cubase plays audible Audio/VST content, use its own Modern Shared Audio Control Panel: select headphones > Save (listen for sound actually moving), then speakers > Save. Repeat 10-20 times; monitor `ASIO_STOP output_device_switches`, error count, callback positions, playback correctness, and MIDI timing. Then test a headset at a different Windows mix rate: previous output should keep playing with a restart notice, not run at an incorrect speed. Also test headset unavailable and fallback, and ASIO stop/start after a route migration. Do not run new code in main until tested.

Portable C++20 unit tests `modern_asio_output_switch_policy_tests` and `modern_asio_output_switch_transaction_tests` cover matching/incompatible sample rates, unchanged routes, stop+open success, failure+rollback, and both devices unavailable. Locally compiled and executed using Clang 17 and GCC 14 with ASan/UBSan: **PASS**. Full Windows MSVC compile, **29 Windows CTests** and real Cubase UI/audio switching remain **UNVERIFIED**. These model tests do not substitute for actual COM device tests.

## Other conditions

- Cubase still controls its ASIO host timeline. Reopening WASAPI may create a short mute and is not guaranteed glitch-free; keep usual 512/1024 buffering.
- The renderer's per-stream underrun/clock counters restart when a new WASAPI stream starts; driver callback counts continue.
- No direct communication to Windows application-volume routing, Chrome audio destination or system default device.
- Restoring a physically disconnected previous speaker may also fail; in that case stop/restart Cubase's audio engine after correcting hardware state.
