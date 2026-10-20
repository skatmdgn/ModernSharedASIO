#pragma once
#include <string>
#include <utility>
namespace modern_asio {
// Distinguish an unavailable pinned endpoint (fallback to Windows default)
// from a genuine selection error (propagate it unchanged). The preferred ID
// remains saved: if the headset reconnects, the next ASIO initialization
// automatically chooses it again.
template<class Result> struct PreferredEndpointAttempt {
    Result result;
    bool unavailable;
};
template<class SelectDefault, class TryPreferred>
auto dispatch_output_endpoint(const std::wstring& preferred_id,
                              SelectDefault&& select_default,
                              TryPreferred&& try_preferred) {
    if(preferred_id.empty())return std::forward<SelectDefault>(select_default)();
    const auto attempt=std::forward<TryPreferred>(try_preferred)(preferred_id);
    if(attempt.unavailable)return std::forward<SelectDefault>(select_default)();
    return attempt.result;
}
}
