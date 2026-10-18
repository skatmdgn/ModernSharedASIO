#pragma once
#include <string>
#include <utility>
namespace modern_asio {
// An explicitly selected endpoint must NEVER silently route to another
// speaker when the headset is disconnected. Propagate its resolution error.
// Empty ID is the only way to request the Windows default output device.
template<class SelectDefault, class SelectById>
auto dispatch_output_endpoint(const std::wstring& id,
                              SelectDefault&& select_default,
                              SelectById&& select_by_id) {
    if(id.empty())return std::forward<SelectDefault>(select_default)();
    return std::forward<SelectById>(select_by_id)(id);
}
}
