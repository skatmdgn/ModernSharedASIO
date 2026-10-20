# Output device selection: headset / speakers / HDMI (Cubase ASIO Control Panel)

2026-10-18 | branch `feature/output-device-picker` based on optimized source `e93e598debd1748df06c44512bfc93494480c2a2` | **Windows/Cubase runtime pending**

## Reported issue and original cause

User connected headphones/headset but couldn't select them in Cubase. Source audit found BOTH IASIO::init and WASAPI render worker called IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender,eConsole) with no choice. IASIO::controlPanel returned ASE_NotPresent. Changing endpoints in ASIO itself was impossible; at most the Windows default output could change after restart.

## Implemented

1. `src/output_device_selector.cpp` provides `resolve_output_endpoint(IMMDeviceEnumerator*,IMMDevice**)`. Reads **HKCU\Software\ModernSharedASIO\OutputDeviceId** (REG_SZ) and resolves the requested IMMDevice::GetId instead of hardcoding Windows default. If the value is missing/empty, the default behavior stays exactly the previous `GetDefaultAudioEndpoint(eRender,eConsole)`.
2. The same resolver is used in the **IASIO initialization** (selected endpoint determines reported mix sample rate) and the **WASAPI renderer worker** (the same saved ID selects the actual audio endpoint). This avoids the original source's hardcoded default in either path. Device state must be `DEVICE_STATE_ACTIVE` for pinned endpoints.
3. `IASIO::controlPanel()` now opens an actual programmatic Win32 dialog: `Windows default output (automatic)` or any currently connected/active `eRender` device, identified with its friendly name. `Refresh` rescans if a headset is plugged in while the panel is open. `Save` writes only HKCU user setting; `Cancel` makes no changes. No official Steinberg driver/registry setting is touched.
4. `modern_asio_output_devices.exe` is a Windows GUI launcher for the same selector outside Cubase. It also permits changing a saved preferred Bluetooth/USB endpoint while the device is disconnected; automatic default-output recovery is now built into the ASIO initialization, so manual clearing is normally unnecessary. this executable is built in `build-win\Debug` even with `MSA_BUILD_ASIO_DLL=OFF`.
5. A **pinned-but-unavailable** endpoint now falls back to the Windows default output during ASIO initialization. Its saved ID remains unchanged, so it is preferred again on the next Cubase startup when connected. If the Windows default also fails, initialization still fails.
6. No hot reroute during active Cubase playback. New device choice applies after fully closing and restarting Cubase/ASIO driver. Changing hardware mix sample rate may require Cubase project/ASIO reinitialization; the driver still correctly refuses rate mismatch.

## Steps on user's Windows PC

Close Cubase, preserve any uncommitted work (git switch may refuse if local edits exist), then build branch:

```powershell
cd C:\ModernSharedASIO
git fetch origin
git switch feature/output-device-picker
git pull --ff-only
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\ModernSharedASIO\scripts\build_windows_x64.ps1' -AsioSdkDir 'C:\sdk\asio' -Configuration Debug
```

Expected CTest count: **27** (previous 26 plus portable `modern_asio_output_device_policy_tests`), Windows run not yet observed.

In Cubase 8, open Devices > Device Setup > VST Audio System > Modern Shared Audio > Control Panel (wording may vary). Choose a currently connected headset, USB audio interface, speakers, or default; click Save. **Exit Cubase completely and start again** before listening.

If the Cubase Control Panel is not accessible or the chosen headset has disconnected, close Cubase and launch:

```powershell
& 'C:\ModernSharedASIO\build-win\Debug\modern_asio_output_devices.exe'
```

To return to automatic Windows default without opening any GUI:

```powershell
Remove-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name OutputDeviceId -ErrorAction SilentlyContinue
```

Then relaunch Cubase.

Check both `ASIO_STOP` output under the exact same project, and listen for correct left/right sound plus MIDI timeline. Test default, built-in speakers, wired headset, USB headset, Bluetooth headset if supported by Windows shared-mode mix format, headset detach/reconnect, switching back, and repeated Cubase restart. Verify no unexpected Windows volume/device change, and no ASIO or WASAPI stream errors.

## Limits and verification

- The fallback occurs at ASIO initialization. It does **not** implement live WASAPI hot swapping or recovery after removal while a stream is already running, ASIO input devices or simultaneous multiple output devices. It selects exactly one Windows render endpoint on next initialization.
- Pinning a headset does NOT automatically reroute Chrome/system sounds to that headset; browser audio still follows its own Windows routing. Both may play simultaneously if the shared-mode endpoint permits.
- The updated portable policy tests cover connected preference, missing preferred -> Windows default, reconnected preference, an actual non-availability error, and no available default. Clang AddressSanitizer+UBSan and GCC Release both PASS locally.
- Complete Windows source (`src/output_device_selector.cpp` and control panel UI) cannot be compiled or interactively verified in this Linux tool environment; user Windows MSVC build, expected 27 CTests and Cubase UI/audio test are **PENDING**. Preserve working `main` and `stable/cubase8-phase2-1024-confirmed` unchanged until user confirms.
- Build uses `user32`, `propsys` and `advapi32` in addition to the previous COM/WASAPI link libraries. Device names are read using `PKEY_Device_FriendlyName`; endpoint choice persists **device ID**, not an unstable friendly label.
- Device selection is an orthogonal usability feature. It does not claim lower sound latency or lower CPU than the full-pass optimization branch.


## 2026-10-20 regression fix: disconnected preferred headset on reboot

**Reproduced user symptom:** Cubase reports `The audio driver could not be loaded` when the selected headset is disconnected at startup.

New initialization decision:

| Saved preference | Availability | Effective ASIO output |
|---|---|---|
| No saved output | — | Windows default |
| Saved headset | Active | Saved headset |
| Saved headset | Disconnected / disabled / missing | Windows default for this Cubase initialization |
| Saved headset | Reconnected after previous fallback | Saved headset on next complete Cubase initialization |
| Saved headset | Disconnected AND no default output exists | Graceful init failure |

Automatic fallback **does not delete** `HKCU\Software\ModernSharedASIO\OutputDeviceId`. It only changes the effective device for the current session. Driver init obtains the effective `IMMDevice::GetId()` and the render worker opens **that exact device** rather than re-resolving a potentially different Windows default. This prevents mixing sample-rate negotiation of one endpoint with actual playback on another. If a device is removed *between* init and start, a full Cubase reinitialization may still be needed; seamless switching while music plays is not implemented.

New `ASIO_STOP output_device_fallback=1` means the saved preference was offline and default was selected; `0` means direct preferred/default without failover. `scripts/summarize_asio_stops.ps1` also displays `OutputFallback` if present. Device IDs are not put into log lines.

**Source/test status:** Updated `modern_asio_output_device_policy_tests` retains 27 expected Windows CTests. Portable test compiled/run with Clang C++20 AddressSanitizer+UndefinedBehaviorSanitizer and GCC C++20 Release: both PASS. Full Windows MSVC DLL build, 27 Windows CTests, and actual Cubase reboot/hardware fallback still **UNVERIFIED**.

Manual test: close Cubase, save a headset, disconnect headset, reboot, and verify Cubase starts with the Windows default device; play actual audio/MIDI; check `output_device_fallback=1`. Reconnect headset, completely restart Cubase, verify preferred playback and `output_device_fallback=0`. Do not change the ASIO 512/FIFO 1024 known-good contract.

To inspect the DLL Cubase has registered (build does NOT automatically install/register a new DLL):
```powershell
(Get-Item 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}\InprocServer32').GetValue('')
```
Never overwrite or unregister the original Steinberg ASIO driver.
