#pragma once
#include <string>
#include <utility>
namespace modern_asio {
enum class OutputSwitchResult { applied, restored_previous, no_usable_output };
// Control thread executes stop -> reopen; no old and new WASAPI consumers
// may access the SPSC ring at the same time.
template<class StopRender,class TryStart>
OutputSwitchResult execute_output_switch(
    const std::wstring& target_id,const std::wstring& previous_id,
    StopRender&& stop_render,TryStart&& try_start) {
    stop_render();
    if(try_start(target_id))return OutputSwitchResult::applied;
    stop_render();
    if(try_start(previous_id))return OutputSwitchResult::restored_previous;
    stop_render();
    return OutputSwitchResult::no_usable_output;
}
}
