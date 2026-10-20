#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <functional>
#include <thread>

namespace modern_asio {
// IMMNotificationClient runs on COM callback threads. The callback only sets
// an event; this control worker performs any endpoint inspection/migration.
// Caller must call stop() before destroying the callback target.
class OutputDeviceChangeMonitor final {
public:
    OutputDeviceChangeMonitor() = default;
    ~OutputDeviceChangeMonitor() {stop();}
    OutputDeviceChangeMonitor(const OutputDeviceChangeMonitor&) = delete;
    OutputDeviceChangeMonitor& operator=(const OutputDeviceChangeMonitor&) = delete;

    bool start(std::function<void()> on_change) noexcept;
    void stop() noexcept;

private:
    void run() noexcept;
    HANDLE stop_event_{nullptr};
    HANDLE changed_event_{nullptr};
    std::thread worker_;
    std::function<void()> on_change_;
};
} // namespace modern_asio
#endif
