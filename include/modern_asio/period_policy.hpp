#pragma once
#include <cstdint>
namespace modern_asio {
struct SharedPeriodRange {
    std::uint32_t default_frames{0};
    std::uint32_t fundamental_frames{0};
    std::uint32_t minimum_frames{0};
    std::uint32_t maximum_frames{0};
};
// Request only integral multiples of the fundamental period.
inline std::uint32_t choose_shared_period(SharedPeriodRange p,
                                         std::uint32_t sample_rate,
                                         std::uint32_t target_ms) noexcept {
    if (!sample_rate || !p.fundamental_frames || !p.minimum_frames ||
        p.minimum_frames > p.maximum_frames) return 0;
    if (target_ms == 0) {
        auto d = p.default_frames;
        return d >= p.minimum_frames && d <= p.maximum_frames &&
               d % p.fundamental_frames == 0 ? d : 0;
    }
    const auto desired = (static_cast<std::uint64_t>(sample_rate) * target_ms + 999) / 1000;
    const auto minimum_quanta = (static_cast<std::uint64_t>(p.minimum_frames) + p.fundamental_frames - 1) /
                                p.fundamental_frames;
    const auto requested_quanta = (desired + p.fundamental_frames - 1) / p.fundamental_frames;
    const auto max_quanta = p.maximum_frames / p.fundamental_frames;
    if (minimum_quanta > max_quanta) return 0;
    const auto quanta = requested_quanta < minimum_quanta ? minimum_quanta :
                        requested_quanta > max_quanta ? max_quanta : requested_quanta;
    return static_cast<std::uint32_t>(quanta * p.fundamental_frames);
}
} // namespace modern_asio
