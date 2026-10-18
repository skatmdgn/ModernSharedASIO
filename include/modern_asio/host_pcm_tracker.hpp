#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "modern_asio/pcm_channel_scan.hpp"
namespace modern_asio {
// One owner: only the ASIO producer calls observe(), and the control thread
// reads snapshot_stopped() AFTER joining the producer thread. No atomic
// read/modify/write operations, locks, allocations or cross-thread races.
struct HostPcmSnapshot {
    std::uint64_t silent_host_blocks{0};
    std::uint64_t first_nonzero_block{UINT64_MAX};
    std::uint64_t last_nonzero_block{UINT64_MAX};
    std::uint64_t nonzero_left_blocks{0};
    std::uint64_t nonzero_right_blocks{0};
    std::uint64_t invalid_float_blocks{0};
    std::uint64_t max_left_peak_ppm{0};
    std::uint64_t max_right_peak_ppm{0};
};
class HostPcmTracker final {
public:
    void reset_stopped() noexcept { stats_={}; }
    void observe(const float* left, const float* right,
                 std::size_t frames,std::uint64_t block_ordinal) noexcept {
        const auto left_scan=scan_pcm_channel(left,frames);
        const auto right_scan=scan_pcm_channel(right,frames);
        const bool left_nonzero=left_scan.nonzero;
        const bool right_nonzero=right_scan.nonzero;
        const bool invalid=left_scan.invalid || right_scan.invalid;
        const float left_peak=left_scan.peak;
        const float right_peak=right_scan.peak;
        if(!left_nonzero && !right_nonzero) ++stats_.silent_host_blocks;
        else {
            if(stats_.first_nonzero_block==UINT64_MAX)
                stats_.first_nonzero_block=block_ordinal;
            stats_.last_nonzero_block=block_ordinal;
        }
        if(left_nonzero)++stats_.nonzero_left_blocks;
        if(right_nonzero)++stats_.nonzero_right_blocks;
        if(invalid)++stats_.invalid_float_blocks;
        const auto to_ppm=[](float peak)->std::uint64_t {
            return static_cast<std::uint64_t>(
                static_cast<double>(std::min(peak,1000.0f))*1000000.0);
        };
        stats_.max_left_peak_ppm=
            std::max(stats_.max_left_peak_ppm,to_ppm(left_peak));
        stats_.max_right_peak_ppm=
            std::max(stats_.max_right_peak_ppm,to_ppm(right_peak));
    }
    HostPcmSnapshot snapshot_stopped() const noexcept{return stats_;}
private:
    HostPcmSnapshot stats_{};
};
}
