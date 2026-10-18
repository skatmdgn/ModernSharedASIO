#pragma once
#include <cstddef>
namespace modern_asio {
// Renderer calls this AFTER consuming its device period. If enough PCM
// remains to satisfy the producer target, waking merely causes an idle
// producer pass with zero blocks to fill. Zero target conservatively wakes.
constexpr bool producer_needs_wakeup(std::size_t queued,
                                    std::size_t target) noexcept {
    return !target || queued<target;
}
}
