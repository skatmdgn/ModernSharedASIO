# ASIO alternating output halves — confirmed host/device contract violation

2026-10-18. User reports **constant 24 bars of forward audio content** at 120 BPM 4/4 in a clean Cubase project, independent of start/stop repetitions; drawn MIDI/VST notes are silent, while live master-keyboard triggering plays normally. This **falsifies/undermines** the prior 'more waiting while Cubase stopped produces more offset' model. Do not claim waiting causes the offset.

## Source-level defect with SDK evidence

Official ASIO SDK `common/asio.h`, ASIOCallbacks::bufferSwitch: the callback's `doubleBufferIndex` means the OUTPUT buffer half the host is STARTING to fill; **the other half** will be passed to output hardware regardless of whether it was ready.

Quote (excerpt): 'the output buffer that the host should start to fill. the other buffer will be passed to output hardware'. See https://github.com/audiosdk/asio/blob/main/common/asio.h

Independent working open-source ASIO host/driver reference FlexASIO (`dechamps/FlexASIO`, `src/flexasio/FlexASIO/flexasio.cpp`, `RunningState::StreamCallback`) invokes the host `bufferSwitch(driverBufferIndex, ASIOTrue)` and then advances the index to the OTHER half before copying to the output device in the non-outputReady path: https://github.com/dechamps/FlexASIO/blob/master/src/flexasio/FlexASIO/flexasio.cpp .

**What our pre-patch source did wrong:** `Driver::produce_one_block` passed `buffer_index_` to Cubase, then immediately submitted exactly the SAME just-written half to `AsioPlaybackBridge`, violating the alternate-half ASIO output protocol. It also copied the B output half to the FIFO once before any callback in `start()`. Both choices were missed by previous mini-host tests because their assertions only checked callback index, samplePosition and a continuous sine—not which half actually reached WASAPI.

**Targeted code correction:**

1. Added `asio_output_half_for_callback(index) == (index ^ 1)` for valid 0/1 indices.
2. After `bufferSwitch(index)`, pass the OPPOSITE ASIO half to the PCM bridge: callback 0 fills A/outputs B, callback 1 fills B/outputs A, callback 2 fills A/outputs B, etc.
3. Removed the eager `bridge_.submit(B)` in `start()` to avoid duplicate B output. WASAPI still primes its device with silence before audio callbacks; first producer wake now submits exactly one B half in response to callback 0.
4. Kept the previously fixed host callback sample position starting 0 and alternating 0/1. The output queue's contents now lag the newly written half by one ASIO period, as required by the contract.
5. Added executable `modern_asio_double_buffer_handoff_tests` exercising 100 callbacks with distinct A/B content, asserting actual FIFO frames follow B(initial) → A(after callback 0) → B(after callback 1) ... . A fake 440 Hz identical tone cannot detect this bug; the new test uses nonidentical content.

## Caveats and next objective

- This is a **confirmed SDK API semantics violation** in the source, NOT proof that it causes a fixed ~24-bar (48-second) musical offset. One ASIO period at 48k/512 is ~10.67ms, so a direct 48-second discrepancy still needs explanation.
- The new `modern_asio_double_buffer_handoff_tests` source is staged; Windows CMake/CTest now expects **14 tests**, and this revision is NOT YET Windows-built or Cubase-tested.
- Do not change original proprietary `asioglld.dll` or Steinberg ASIO registration; this commit affects only ModernSharedASIO source and tests.
- With this proven handoff fix compiled and re-tested in a copy of the clean project, if fixed 24-bar mismatch persists, next required engineering step is a bar-anchored PCM provenance trace at Cubase ASIO buffer delivery vs WASAPI submission, not more guessed timer changes. Preserve user evidence and stop recycling failed hypotheses.

Status: source defect fixed and specialized unit test written; Cubase offset unresolved until real-device check.

## 2026-10-18 — Cubase user confirms successful functional retest

**USER-REPORTED PASS after commit d3d770b308b7055763da1d9c04900ed60d7e243e.** The user responded, “됐어! 이제 모든게 잘나와” (“It works! Now everything plays correctly”). This follows the previous clean-project repro: fixed ~24-bar forward Audio content, drawn MIDI/VST notes silent, and live controller VST working. The user says the audio output is now fully working after correcting the ASIO double-buffer handoff. That is meaningful on-machine acceptance evidence for Cubase 8.0.40; the exact separate item-by-item checks were not pasted, so treat the overarching PASS as user attestation, not independently reproduced instrumented tests.

**Causal status:** the change most closely linked to recovery is the **opposite-half output handoff plus removal of redundant B prefill**, correcting a demonstrated ASIO SDK contract violation. These were applied together, so the improvement does not isolate the relative contribution of each edit. Source bug and observed behavior are strongly consistent, though an exact 24-bar displacement mechanism has not been mathematically demonstrated.

**Still pending before a general release:** inspect a full Windows 14/14 CTest report and exact full-session logs for this commit; test stop/restart, sample rates, high track count and long-run resilience. Never claim those checks happened without results. Original Steinberg DLL remains unchanged.
