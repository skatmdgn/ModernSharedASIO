# Output device selection: headset / speakers / HDMI (Cubase ASIO Control Panel)

2026-10-18 | branch `feature/output-device-picker` based on optimized source `e93e598debd1748df06c44512bfc93494480c2a2` | **Windows/Cubase runtime pending**

## Reported issue and original cause

User connected headphones/headset but couldn't select them in Cubase. Source audit found BOTH IASIO::init and WASAPI render worker called IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender,eConsole) with no choice. IASIO::controlPanel returned ASE_NotPresent. Changing endpoints in ASIO itself was impossible; at most the Windows default output could change after restart.

## Implemented

1. `src/output_device_selector.cpp` provides `resolve_output_endpoint(IMMDeviceEnumerator*,IMMDevice**)`. Reads **HKCU\Software\ModernSharedASIO\OutputDeviceId** (REG_SZ) and resolves the requested IMMDevice::GetId instead of hardcoding Windows default. If the value is missing/empty, the default behavior stays exactly the previous `GetDefaultAudioEndpoint(eRender,eConsole)`.
2. The same resolver is used in the **IASIO initialization** (selected endpoint determines reported mix sample rate) and the **WASAPI renderer worker** (the same saved ID selects the actual audio endpoint). This avoids the original source's hardcoded default in either path. Device state must be `DEVICE_STATE_ACTIVE` for pinned endpoints.
3. `IASIO::controlPanel()` now opens an actual programmatic Win32 dialog: `Windows default output (automatic)` or any currently connected/active `eRender` device, identified with its friendly name. `Refresh` rescans if a headset is plugged in while the panel is open. `Save` writes only HKCU user setting; `Cancel` makes no changes. No official Steinberg driver/registry setting is touched.
4. `modern_asio_output_devices.exe` is a Windows GUI launcher for the same selector outside Cubase. It is important for recovering from a saved Bluetooth/USB headset ID that is temporarily disconnected and causes driver initialization to fail; this executable is built in `build-win\Debug` even with `MSA_BUILD_ASIO_DLL=OFF`.
5. A **pinned-but-unavailable** endpoint returns an error instead of silently playing through speakers. Reset to Windows default with the companion app or by deleting the registry override.
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

- The output picker does not implement live WASAPI endpoint hot swapping, auto-recovery from unexpected unplug during a running stream, ASIO input devices or simultaneous multiple output devices. It selects exactly one Windows render endpoint on next initialization.
- Pinning a headset does NOT automatically reroute Chrome/system sounds to that headset; browser audio still follows its own Windows routing. Both may play simultaneously if the shared-mode endpoint permits.
- New policy C++20 regression tests dispatch the missing ID to the Windows default branch and explicit/offline ID only to the pinned branch (no failover). Clang AddressSanitizer+UBSan and GCC Release both PASS locally.
- Complete Windows source (`src/output_device_selector.cpp` and control panel UI) cannot be compiled or interactively verified in this Linux tool environment; user Windows MSVC build, expected 27 CTests and Cubase UI/audio test are **PENDING**. Preserve working `main` and `stable/cubase8-phase2-1024-confirmed` unchanged until user confirms.
- Build uses `user32`, `propsys` and `advapi32` in addition to the previous COM/WASAPI link libraries. Device names are read using `PKEY_Device_FriendlyName`; endpoint choice persists **device ID**, not an unstable friendly label.
- Device selection is an orthogonal usability feature. It does not claim lower sound latency or lower CPU than the full-pass optimization branch.

