# Windows MSVC build/debug handoff (2026-10-18)

- GitHub main base `84e7f78c` before these fixes.
- User Windows compile: Visual Studio 2022 MSVC 19.44.35228.0, Windows SDK 10.0.26100.0; configure and WASAPI backend + smoke passed.
- Blocker: errors `C2375 DllGetClassObject` and `C2375 DllCanUnloadNow` (combaseapi.h redeclares entrypoints via STDAPI).
- Fix: matching `STDAPI` signatures in driver source and explicit MSVC `.def` for four COM exports; C4005 macro guards.
- Preflight verifies all export addresses but **does not invoke DllRegisterServer or DllUnregisterServer**.
- Next user command after `git pull`: `./scripts/build_windows_x64.ps1 -AsioSdkDir C:/sdk/asio -Configuration Debug` (in PowerShell; if execution policy requires, use `powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build_windows_x64.ps1 -AsioSdkDir C:\\sdk\\asio -Configuration Debug`).
- Expected: MSVC build then CTest (8 portable tests + 1 registry-free COM preflight). Windows compilation and tests **not yet verified** after this fix.
- Never overwrite original Steinberg `asioglld.dll`; do not register until controlled validation.
