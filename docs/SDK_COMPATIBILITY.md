# ASIO SDK interface details and license

This project consumes **external**, user-provided official Steinberg ASIO SDK 2.3 headers (`common/iasiodrv.h`, `common/asio.h`, `common/asiosys.h`). No copies are vendored.

Source reference: https://github.com/audiosdk/asio

## Compatibility finding

The official ASIO SDK host implementation `host/pc/asiolist.cpp` invokes:

```cpp
CoCreateInstance(driver_clsid, nullptr, CLSCTX_INPROC_SERVER, driver_clsid, &driver);
```

Thus the ASIO driver's `QueryInterface` must recognize its **own CLSID as an interface ID** in addition to `IID_IUnknown`. The 24-slot IASIO interface follows the SDK's `iasiodrv.h` method order.

Current COM driver is source-only, not Windows-built or Cubase-validated. Explicit `DllRegisterServer` / `DllUnregisterServer` are implemented, but must NOT be invoked prior to build/contract validation.

## Licensing and naming

The current ASIO SDK offers GPLv3 and a separate proprietary licensing path. Before distributing any binary, decide a license model, include required license notices, and verify SDK and trademark compliance. No Steinberg or third-party proprietary DLL code is included. The repository name chosen by the owner is not an assertion that Steinberg endorses or certifies this software.
