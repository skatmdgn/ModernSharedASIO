#pragma once
#include <cstdint>
#include <string>
namespace modern_asio {
enum class OutputSwitchDecision { same_device, restart_renderer, incompatible_sample_rate, invalid_endpoint };
inline OutputSwitchDecision decide_output_switch(
    const std::wstring& current_endpoint,
    const std::wstring& selected_endpoint,
    std::uint32_t asio_rate,
    std::uint32_t selected_rate) noexcept {
    if(selected_endpoint.empty()||!asio_rate||!selected_rate)
        return OutputSwitchDecision::invalid_endpoint;
    if(asio_rate!=selected_rate)
        return OutputSwitchDecision::incompatible_sample_rate;
    if(current_endpoint==selected_endpoint)
        return OutputSwitchDecision::same_device;
    return OutputSwitchDecision::restart_renderer;
}
} // namespace modern_asio
