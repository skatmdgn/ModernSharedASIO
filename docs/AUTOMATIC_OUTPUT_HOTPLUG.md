# Automatic output hotplug — isolated experimental branch

2026-10-20 | feature/automatic-output-hotplug-20261020 (branched from feature/output-device-picker)

## User-reported requirement

Cubase 8.0.40 should play through the headset actually chosen in Modern Shared Audio, not stay on the speakers. Disconnecting that headset must not prevent the next Cubase launch, and an active Cubase playback session should use the Windows default output as fallback where safe. Reconnecting the pinned headset should restore that preference if the physical clock is compatible.

## Changes

- New IMMNotificationClient subscribing to render endpoint state/add/remove changes and changes to the Windows **eConsole** default output; supported callback methods only **SetEvent**, and never touch IASIO, WASAPI, the PCM buffers or the registry inside a COM notification callback.
- Dedicated non-realtime COM MTA worker waits for notifications, coalesces a ~125ms burst, resolves the actual saved preferred output or Windows default fallback, reads its Windows mix format and applies the same route-switch/rollback logic as Cubase Control Panel Save. At ASIO init it also performs a reconciliation after listener registration.
- A single Driver lifecycle mutex serializes route migration with host start(), stop() and controlPanel(). Renderer.stop() joins the previous consumer before renderer.start() opens a new endpoint; the Cubase ASIO producer stays alive. No changes to alternating ASIO buffer half, 512 frame size, FIFO target 1024, playback PCM, producer timeline, or WASAPI shared-mode format handling.
- Devices at an incompatible mix **sample rate** are deliberately not switched live: this driver does not yet resample or negotiate a new ASIO clock with the already-running Cubase 8 session. The existing output route is kept when possible; if the old device has been unplugged, playback can be silent until the user matches Windows device sample rates and restarts the ASIO engine.
- If new rendering cannot open, attempt rollback to the old device. If neither opens, the existing ASIO producer continues but no WASAPI audio can be heard. The next add/state/default notification allows a later retry.
- Unplugged saved preference is **not overwritten** in HKCU. Plugging it back in should trigger a return to it. The standalone preference picker still only saves user choice; it does not directly message an already-loaded Cubase DLL.
- A failed monitor subscription does not block ASIO initialization; a manually selected route can still be applied through the embedded Control Panel.

## Verification status

**Source implementation only.** No Windows x64 MSVC build, runtime Windows COM subscription, Windows CTest suite, real Cubase 8 audio, physical USB/Bluetooth headset switch, or cross-sample-rate acceptance has been verified in this session. This branch is NOT merged into main or stable.

Before tests close Cubase, build the branch, verify DLL registration points to that exact built binary with scripts/diagnose_output_route.ps1, then reopen Cubase. Repeated tests:

1. Speaker 48k and headset 48k with continuous audio plus VST/MIDI, use Control Panel Save and listen for true output changes; repeat 20 switches.
2. Keep headset preferred, unplug it during playback; expect fallback to Windows default speakers after device notifications settle, without crashing.
3. Reconnect headset; expect return to selected headset if both endpoints report identical mix rates.
4. Select Windows-default automatic preference, then change the Windows default output; confirm device moves.
5. Set headset to 44.1k while Cubase is 48k; the driver must refuse clock-incompatible migration, without wrong-speed playback.
6. Repeat stop/start, unplug/reboot/reconnect and driver release; inspect stream errors and output_device_switches / output_device_switch_failures in ASIO_STOP telemetry. Verify no COM callback-after-destruction and no stale registered DLL.

Known limitations: This is a controlled stop/reopen, NOT click-free or sample-accurate crossfade. With both old and new endpoints gone, audio may remain silent while Cubase stays running. No automatic resampling and no ASIO reset-request negotiation yet. Host hardware notifications may fail in some environments; logs and Windows Cubase verification are required before declaring complete.
