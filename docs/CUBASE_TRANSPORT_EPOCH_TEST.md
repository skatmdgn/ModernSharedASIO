# New clean-project 120 BPM reproduction: 24 bars forward for Audio, sequenced MIDI silent

**2026-10-18 (KST) — direct user observation, not a theory**

- Cubase 8.0.40 newly created empty project, 4/4, 120 BPM.
- Audio tracks: with playhead at bar 0, actual output corresponds to source material approximately **24 bars later**.
- MIDI/VST: notes drawn/programmed on the Cubase timeline yield **no sound at all** during transport playback.
- The SAME or comparable VST instrument controlled live from a physical master keyboard **does sound correctly and without obvious delay**.
- Earlier project had ~16-bar forward offset and multi-track load dependence.

## Time conversion and specific hypothesis

120 BPM x 4/4 = 2 seconds per bar. **24 bars = 48 seconds = 2,304,000 frames at 48kHz = 4,500 ASIO blocks at 512 frames**. Earlier 16 bars = 32 seconds = 1,536,000 frames = 3,000 ASIO blocks. An exact whole-ASIO-block alignment of these numbers is noteworthy; this is arithmetic, not measured callback count.

**New discriminating hypothesis:** If audio engine ASIO callbacks advance continuously while Cubase transport is stopped (normal for ASIO hardware playback), but the host incorrectly uses unadjusted absolute ASIO stream time as a musical sequencer playhead when transport is started/repositioned, then a forward offset proportional to *real engine elapsed time before pressing Play* may occur. 24 bars could reflect 48 seconds between engine activation and transport-start; earlier 16 bars could reflect 32 seconds. This is NOT yet demonstrated; the time spent preparing each project wasn't recorded. The driver cannot directly read Cubase's bar position.

## One-variable test — no code changes, no extra build

1. In a disposable project, preserve 4/4 at 120 BPM. Put a distinctive short AUDIO clip or click exactly at bar 25, leaving bars 1–24 empty; track output routed to the experimental driver as usual.
2. Set Cubase transport position to bar 1 (the user calls this zero). **After opening Cubase and selecting the experimental ASIO driver, play promptly**, note what bar's sound is heard.
3. Stop and set transport to exactly the same beginning position. Leave Cubase OPEN and transport stopped for exactly **30 seconds** (while the ASIO device remains loaded), then Play again. Does the forward offset grow by **15 bars** (30s x 2bps / 4 beats per bar), stay fixed, or change unpredictably?
4. Optional: repeat after 60 seconds idle (30 bars). Note whether the previously clean audio start now plays later musical content immediately. Keep the driver/tempo/sample rate unchanged. This is a *causality test*, not a fix.
5. The source commit installed on user Windows is unknown: no current `modern_asio_host_smoke`/CTest output proving the 2026-10-18 time-pair code is loaded. Do not assume recent GitHub source equals running DLL.

## Interpret outcomes

- Offset grows approximately 15 bars after 30 seconds idle: strong evidence of a missing engine-to-transport epoch translation, to be isolated in Cubase-ASIO samplePosition/time contract rather than generic ring buffering.
- Offset remains constant: time-since-ASIOStart alone cannot explain the symptom; shift attention to Cubase synchronization configuration, transport status, plugin delay compensation and audio content identity.
- MIDI sequencer remains silent but live controller sound normal regardless: consistent with timeline-event scheduling broken, NOT VST synth or speaker failure. Driver sees only PCM and cannot directly schedule Cubase MIDI notes.

**Severity:** actual Cubase multitrack/sequence quality gate remains FAILED. Never claim an ASIO software fix based on this observation alone. Do not modify original Steinberg DLL or registry keys. No CI until final gate.
