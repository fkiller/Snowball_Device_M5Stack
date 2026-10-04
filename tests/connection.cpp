#include "../firmware/connection.h"
#include "../firmware/i18n.h"
#include "../firmware/hangul.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace snowball;
int main(){
  ConnectionFlow flow;
  flow.wifi(100);assert(flow.update(20099,false)==ConnectionResult::None);
  assert(flow.update(20100,false)==ConnectionResult::WifiFailed);
  flow.wifi(100);flow.authenticated(110);assert(flow.phase==ConnectionPhase::Wifi);
  flow.middleware(150);assert(flow.update(15149,true)==ConnectionResult::None);
  assert(flow.update(15150,true)==ConnectionResult::MiddlewareFailed);
  flow.middleware(200);assert(flow.update(201,false)==ConnectionResult::WifiFailed);
  flow.middleware(300);flow.authenticated(400);
  assert(flow.update(1399,true)==ConnectionResult::None);
  flow.authenticated(1000);assert(flow.changedAt==400); // polling cannot extend hold
  assert(flow.update(1400,true)==ConnectionResult::OpenSession);
  flow.wifi(0xfffffff0);assert(flow.update(0x20,false)==ConnectionResult::None);
  flow.cancel();assert(flow.update(50000,false)==ConnectionResult::None);
  TextInput input;input.korean=true;input.append('g');input.append('k');
  const auto draft=input.text();
  assert(std::strcmp(translate(Ui::WifiSettings,false),"Wi-Fi settings")==0);
  assert(std::strcmp(translate(Ui::WifiSettings,true),"Wi-Fi 설정")==0);
  assert(input.korean&&input.text()==draft); // rendering language never mutates IME
  std::cout<<"Observed connection transitions, deadlines, wraparound and locale/input isolation passed\n";
}
