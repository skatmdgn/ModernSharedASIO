#include "modern_asio/output_device_selection_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
int main() {
    int defaults=0,explicit_calls=0;
    const auto d=modern_asio::dispatch_output_endpoint(std::wstring{},
        [&]{++defaults;return 100;},
        [&](const std::wstring&){++explicit_calls;return 200;});
    CHECK(d==100 && defaults==1 && explicit_calls==0);
    const auto p=modern_asio::dispatch_output_endpoint(
        std::wstring{L"{HEADSET-ID}"},
        [&]{++defaults;return 100;},
        [&](const std::wstring& id){
            ++explicit_calls;
            CHECK(id==L"{HEADSET-ID}");
            return -7;
        });
    CHECK(p==-7 && defaults==1 && explicit_calls==1);
    // Explicit headset missing => fail, do NOT unexpectedly switch speakers.
    const auto error=modern_asio::dispatch_output_endpoint(
        std::wstring{L"unplugged-headset"},
        [&]{++defaults;return 0;},
        [&](const std::wstring&){++explicit_calls;return -123;});
    CHECK(error==-123 && defaults==1 && explicit_calls==2);
    std::puts("output endpoint default/pinned no-fallback policy PASS");
}
