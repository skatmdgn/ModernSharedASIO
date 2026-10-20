#include "modern_asio/output_switch_transaction.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d %s\n",__LINE__,#x);std::abort();}}while(false)
int main(){
 using modern_asio::OutputSwitchResult;
 using modern_asio::execute_output_switch;
 auto run=[](bool target_ok,bool old_ok,OutputSwitchResult expected,
             std::vector<std::wstring> desired){
  std::vector<std::wstring> events;
  bool active=true;
  auto stop=[&]{events.push_back(L"stop");active=false;};
  auto start=[&](const std::wstring& id){
    events.push_back(id);CHECK(!active);
    bool ok=id==L"headset"?target_ok:old_ok;
    active=ok;return ok;
  };
  const auto result=execute_output_switch(L"headset",L"speakers",stop,start);
  CHECK(result==expected);CHECK(events==desired);
  CHECK(active==(result!=OutputSwitchResult::no_usable_output));
 };
 run(true,true,OutputSwitchResult::applied,{L"stop",L"headset"});
 run(false,true,OutputSwitchResult::restored_previous,
     {L"stop",L"headset",L"stop",L"speakers"});
 run(false,false,OutputSwitchResult::no_usable_output,
     {L"stop",L"headset",L"stop",L"speakers",L"stop"});
 std::puts("ASIO live output switch stop/reopen/rollback policy PASS");
}
