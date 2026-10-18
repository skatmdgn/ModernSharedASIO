// Test experimental DLL factory without writing any registry keys.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#include <iasiodrv.h>
#include <cstdio>
using GetFactory = HRESULT (STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);
using CanUnload = HRESULT (STDAPICALLTYPE*)(void);
constexpr CLSID kCLSID{0x6fa63f59,0xa581,0x4d9a,{0xb8,0xcb,0x73,0xc3,0xbd,0x3a,0x6d,0x46}};
int wmain(int count,wchar_t** args) {
    if(count!=2) {std::puts("Usage: modern_asio_preflight path-to-ModernSharedASIO.dll");return 2;}
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE){std::printf("COM init: 0x%08lx\n",static_cast<unsigned long>(init));return 1;}
    auto module=LoadLibraryW(args[1]);
    if(!module){std::printf("DLL load failed: %lu\n",GetLastError());if(SUCCEEDED(init))CoUninitialize();return 1;}
    auto get=reinterpret_cast<GetFactory>(GetProcAddress(module,"DllGetClassObject"));
    auto can_unload=reinterpret_cast<CanUnload>(GetProcAddress(module,"DllCanUnloadNow"));
    const auto register_export=GetProcAddress(module,"DllRegisterServer");
    const auto unregister_export=GetProcAddress(module,"DllUnregisterServer");
    if(!get||!can_unload||!register_export||!unregister_export) {
        std::printf("Missing required DLL exports: factory=%d unload=%d register=%d unregister=%d\n",
                    get!=nullptr,can_unload!=nullptr,register_export!=nullptr,unregister_export!=nullptr);
        // Registration entrypoints are inspected by name, NEVER invoked.
        FreeLibrary(module);
        if(SUCCEEDED(init))CoUninitialize();
        return 1;
    }
    std::puts("All four COM DLL exports found; no registry changes.");
    IClassFactory* factory=nullptr;
    HRESULT hr=get?get(kCLSID,IID_IClassFactory,reinterpret_cast<void**>(&factory)):E_NOINTERFACE;
    IASIO* asio=nullptr;
    if(SUCCEEDED(hr))hr=factory->CreateInstance(nullptr,kCLSID,reinterpret_cast<void**>(&asio));
    if(SUCCEEDED(hr)) {
        char name[32]{};
        asio->getDriverName(name);
        long input=-1,output=-1;
        auto channel_status=asio->getChannels(&input,&output);
        std::printf("COM OK: %s v%ld; channel query=%ld in=%ld out=%ld\n",
                    name,asio->getDriverVersion(),static_cast<long>(channel_status),input,output);
        asio->Release();
    } else std::printf("COM factory FAILED: 0x%08lx\n",static_cast<unsigned long>(hr));
    if(factory)factory->Release();
    FreeLibrary(module);
    if(SUCCEEDED(init))CoUninitialize();
    return SUCCEEDED(hr)?0:1;
}
#endif
