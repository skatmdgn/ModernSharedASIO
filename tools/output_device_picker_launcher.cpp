#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "modern_asio/output_device_selector.hpp"
// Standalone companion picker for cases where Cubase refuses to initialize an
// ASIO driver due to a disconnected pinned headset. No driver registration,
// sound playback, or permanent changes except saving the explicit selection.
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    modern_asio::show_output_device_picker(nullptr);
    return 0;
}
#endif
