# Windows MSVC build/debug handoff (2026-10-18)

- GitHub main base `84e7f78c` before these fixes.
- User Windows compile: Visual Studio 2022 MSVC 19.44.35228.0, Windows SDK 10.0.26100.0; configure and WASAPI backend + smoke passed.
- Blocker: errors `C2375 DllGetClassObject` and `C2375 DllCanUnloadNow` (combaseapi.h redeclares entrypoints via STDAPI).
- Fix: matching `STDAPI` signatures in driver source and explicit MSVC `.def` for four COM exports; C4005 macro guards.
- Preflight verifies all export addresses but **does not invoke DllRegisterServer or DllUnregisterServer**.
- Next user command after `git pull`: `./scripts/build_windows_x64.ps1 -AsioSdkDir C:/sdk/asio -Configuration Debug` (in PowerShell; if execution policy requires, use `powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build_windows_x64.ps1 -AsioSdkDir C:\\sdk\\asio -Configuration Debug`).
- Expected: MSVC build then CTest (8 portable tests + 1 registry-free COM preflight). Windows compilation and tests **not yet verified** after this fix.
- Never overwrite original Steinberg `asioglld.dll`; do not register until controlled validation.

## Resolved: MSVC Debug build and COM factory preflight passed (2026-10-18)

**Evidence: user-provided complete local Windows PowerShell log**, repository HEAD `4a6a68eb` (fast-forward confirmed by git pull and git log).

- CMake configure/generate succeeded; MSVC `19.44.35228.0`, Windows SDK `10.0.26100.0`, target Windows `10.0.22631`.
- `ModernSharedASIO.vcxproj -> C:\ModernSharedASIO\build-win\Debug\ModernSharedASIO.dll` succeeded.
- WASAPI static backend, `modern_asio_smoke.exe`, `modern_asio_preflight.exe`, and all eight portable test executables built.
- `ctest --test-dir build-win -C Debug --output-on-failure`: **9/9 PASSED**, including COM factory preflight, total reported runtime 0.51 seconds.
- **No C2375 errors** after matching the SDK's STDAPI declarations.
- Remaining: four **nonfatal** linker warnings `LNK4104` for COM DLL exports; these may be addressed later with `PRIVATE` in the .def file but do not prevent GetProcAddress or the successful COM preflight.
- Tests only exercise portable code and registration-free COM creation; the user has **not yet run actual WASAPI playback**, installed the driver, opened Cubase with it, or compared simultaneous YouTube playback.

**Immediate next test (do not register DLL):** `cd C:\ModernSharedASIO; .\build-win\Debug\modern_asio_smoke.exe --tone` with Windows output sound at a moderate volume. Then retry while YouTube is playing, and capture output stats/HRESULT/clock. The 440 Hz test tone is approximately -36 dBFS for five seconds. `underrun_frames` may reveal initial scheduling issues and is not automatically a CTest failure. Do not assert audio functionality until tested by user.

**Phase gate:** `BUILD_VERIFIED=true (Debug x64)`, `COM_FACTORY_PREFLIGHT=true`, `WASAPI_RUNTIME_VERIFIED=false`, `CUBASE_VERIFIED=false`, `PERFORMANCE_COMPARISON=false`. Release x64 is not yet built.
