#include "modern_asio/output_switch_policy.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
int main() {
    using modern_asio::decide_output_switch;
    using modern_asio::OutputSwitchDecision;
    CHECK(decide_output_switch(L"speakers",L"headset",48000,48000)==OutputSwitchDecision::restart_renderer);
    CHECK(decide_output_switch(L"speakers",L"speakers",48000,48000)==OutputSwitchDecision::same_device);
    CHECK(decide_output_switch(L"speakers",L"headset",48000,44100)==OutputSwitchDecision::incompatible_sample_rate);
    CHECK(decide_output_switch(L"speakers",L"",48000,48000)==OutputSwitchDecision::invalid_endpoint);
    CHECK(decide_output_switch(L"speakers",L"headset",0,48000)==OutputSwitchDecision::invalid_endpoint);
    CHECK(decide_output_switch(L"speakers",L"headset",48000,0)==OutputSwitchDecision::invalid_endpoint);
    // The default-device setting still resolves a non-empty actual endpoint
    // before we get here. No silent policy to mutate the ASIO sample rate.
    CHECK(decide_output_switch(L"headset",L"speakers",48000,48000)==OutputSwitchDecision::restart_renderer);
    std::puts("output switch route and sample-rate contract PASS");
}
