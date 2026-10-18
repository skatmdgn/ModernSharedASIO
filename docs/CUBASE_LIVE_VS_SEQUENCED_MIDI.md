# Cubase 8 — Live MIDI works; sequenced MIDI wrong musical time

**User observation (2026-10-18):** When triggering the very same virtual instrument by a physical MIDI master keyboard, playback is immediately audible and works normally. However, MIDI notes drawn/programmed in the Cubase arrangement play with wrong musical placement, including later musical passages sounding when the playhead starts over an empty section at bar 0; the problem worsens as more tracks are enabled. Previous raw WAV tracks were not consistently affected in the same way. This is evidence for a **live-vs-sequenced MIDI processing path differential**, not 16 bars of ordinary audio interface latency.

## Grounding

- Steinberg Cubase documentation and Steinberg Forum Cubase 8-era discussion describe record-enabled/monitored instrument channels executing in real time while other eligible VSTi/audio tracks can be pre-processed by ASIO-Guard: https://forums.steinberg.net/t/understanding-asio-guard-levels/69110 and https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/optimizing/optimizing_asio_guard_c.html?contentId=0Xcja8v8M5Q6OPuu~DUsyQ .
- The user earlier described no improvement from multiple driver patches and an attempted ASIO-Guard change, so **ASIO-Guard alone is not proven**. Do not repeat an undirected ASIO-Guard OFF recommendation as though new.
- A Windows ASIO audio driver does NOT receive Cubase's MIDI note event timeline or MIDI transport bar number. It receives timestamped/untimestamped audio buffer callbacks and only the resultant stereo PCM. Consequently, it cannot directly identify which Cubase MIDI note was played unless the host side or a controlled audio/MIDI marker test provides that relationship.
- Live keyboard trigger working means sample generation, VST plug-in audio output, host-to-ASIO PCM transfer and WASAPI output all function **in at least the live processing path**. It does not certify them under the sequenced path.
- Existing ASIO host smoke uses a simple constant 440 Hz tone and therefore cannot verify instrument MIDI note scheduling or multi-track ASIO-Guard pre-processing. A passing standalone host test is NOT evidence of Cubase programmed-MIDI correctness.

## One minimal discriminating test without rebuilding

On a COPY of the affected project, select **the SAME instrument track with the SAME programmed MIDI part**. Keep all MIDI notes and project tempo unchanged. With Cubase transport STOPPED, set that exact track's Record Enable **ON** (red record-enable button), but **DO NOT press the global Record button**. Press regular PLAY from bar 0. Compare the future-bar onset to the same project's track with Record Enable OFF. The record-enabled track may be forced onto the real-time path; other tracks stay as configured. This tests the processing-path distinction while holding the driver, instrument, notes and routing constant. Restore Record Enable OFF afterwards. If it corrects the scheduled MIDI offset, prioritize Cubase real-time-vs-prerender scheduling/ASIO-Guard interaction; if unchanged, investigate timestamped audio callbacks and Cubase sequencing/transport independently of ASIO-Guard.

Do NOT alter both Record Enable and ASIO-Guard settings together; do not record over existing MIDI material. There is no need to reinstall the ASIO DLL or change sample rates to perform this discriminator.

## Engineering direction

1. Do **NOT** claim the previous samplePosition/+512, outputReady, TimeInfo clock-pair, or zeroing changes cured this; the user confirmed no improvement on older revisions. The most recent time-pair/zeroing revision is Windows-runtime **unverified**.
2. Distinguish whether an already-armed programmed MIDI part plays on time versus unarmed programmed MIDI part with the same notes. Establish whether specific VSTi record/monitor state changes the result.
3. If the error happens on both paths, add bounded, opt-in per-callback PCM fingerprint tracing with start/stop event time and ASIO sample position plus a user-controlled Cubase bar-0 marker. Observe whether incorrect future audio exists in Cubase's ASIO buffer before the FIFO or appears only after the FIFO/WASAPI conversion. No realtime disk I/O.
4. If only programmed-MIDI/ASIO-Guard path fails, inspect Cubase plug-in delay compensation and ASIO hardware latency stability, ASIO callback timing/clock correlation, and differences between record-armed/unarmed routes. Avoid invented claims that the ASIO driver directly sends incorrect MIDI notes.

**Status: user evidence grounded; cause not identified, Windows/Cubase workaround not confirmed, no source patch in this note.**
