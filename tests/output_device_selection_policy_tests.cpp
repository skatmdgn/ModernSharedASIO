#include "modern_asio/output_device_selection_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(false)
int main() {
    using modern_asio::PreferredEndpointAttempt;
    int defaults=0,pinned=0;
    auto pick_default=[&]{++defaults;return 100;};
    const auto no_preference=modern_asio::dispatch_output_endpoint(
        std::wstring{},pick_default,
        [&](const std::wstring&){++pinned;return PreferredEndpointAttempt<int>{200,false};});
    CHECK(no_preference==100 && defaults==1 && pinned==0);
    const auto connected=modern_asio::dispatch_output_endpoint(
        std::wstring{L"{HEADSET-ID}"},pick_default,
        [&](const std::wstring& id){
            CHECK(id==L"{HEADSET-ID}");++pinned;
            return PreferredEndpointAttempt<int>{200,false};
        });
    CHECK(connected==200 && defaults==1 && pinned==1);
    // Headset unplugged: one attempt then Windows default, not ASIO init failure.
    const auto unplugged=modern_asio::dispatch_output_endpoint(
        std::wstring{L"{HEADSET-ID}"},pick_default,
        [&](const std::wstring&){++pinned;return PreferredEndpointAttempt<int>{-7,true};});
    CHECK(unplugged==100 && defaults==2 && pinned==2);
    // A subsequent restart/replug must still prefer the original saved ID.
    const auto replugged=modern_asio::dispatch_output_endpoint(
        std::wstring{L"{HEADSET-ID}"},pick_default,
        [&](const std::wstring&){++pinned;return PreferredEndpointAttempt<int>{201,false};});
    CHECK(replugged==201 && defaults==2 && pinned==3);
    // A non-availability error (e.g. releasing an interface) is not hidden.
    const auto error=modern_asio::dispatch_output_endpoint(
        std::wstring{L"{HEADSET-ID}"},pick_default,
        [&](const std::wstring&){++pinned;return PreferredEndpointAttempt<int>{-9,false};});
    CHECK(error==-9 && defaults==2 && pinned==4);
    const auto no_default=modern_asio::dispatch_output_endpoint(
        std::wstring{L"offline"},[&]{++defaults;return -123;},
        [&](const std::wstring&){++pinned;return PreferredEndpointAttempt<int>{-4,true};});
    CHECK(no_default==-123 && defaults==3 && pinned==5);
    std::puts("output preference/automatic fallback/reconnect/error policy PASS");
}
