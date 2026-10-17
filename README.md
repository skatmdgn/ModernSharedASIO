# ModernSharedASIO

**Status: early development, NOT yet an ASIO DLL.** This repository is a standalone Windows x64 shared-mode audio driver project intended to support Cubase 8.0.40 while Windows/YouTube audio also plays.

The first development batch adds a preallocated SPSC stereo buffer, a shared engine period selector, a WASAPI event-driven render backend source (IAudioClient3 with shared-mode fallback), a diagnostic output executable and local tests.

The proprietary original Steinberg DLL is not included; `skatmdgn/ASIOGLLD` is an evidence reference.

See `docs/ROADMAP.md` after the initial import.