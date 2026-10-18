#pragma once
#include <cstdint>

namespace modern_asio {
// QPC frequency is constant during a Windows boot. Keep all arithmetic
// integer-based and preserve the old split-quotient conversion for ns.
constexpr std::uint64_t qpc_ticks_to_ns(std::uint64_t ticks,
                                        std::uint64_t frequency) noexcept {
    if (!frequency) return 0;
    return (ticks/frequency)*1000000000ULL+
           ((ticks%frequency)*1000000000ULL)/frequency;
}
// Compute callback duration directly from TWO raw QPC ticks, with just one
// conversion rather than converting both absolute timestamps to ns.
constexpr std::uint64_t qpc_elapsed_us(std::uint64_t begin,
                                       std::uint64_t end,
                                       std::uint64_t frequency) noexcept {
    if (!frequency || end<begin) return 0;
    const auto delta=end-begin;
    return (delta/frequency)*1000000ULL+
           ((delta%frequency)*1000000ULL)/frequency;
}
}
