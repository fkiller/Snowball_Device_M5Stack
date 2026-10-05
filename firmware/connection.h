#pragma once
#include <stdint.h>
namespace snowball {
enum class ConnectionPhase { Idle, Wifi, Middleware, Connected };
enum class ConnectionResult { None, WifiFailed, MiddlewareFailed, OpenSession };
// Only observed association and an authenticated connected view advance this
// flow. Deadlines report failure; elapsed time never fabricates success.
struct ConnectionFlow {
  ConnectionPhase phase=ConnectionPhase::Idle;
  uint32_t changedAt=0;
  void wifi(uint32_t now){phase=ConnectionPhase::Wifi;changedAt=now;}
  void middleware(uint32_t now){phase=ConnectionPhase::Middleware;changedAt=now;}
  void authenticated(uint32_t now){if(phase==ConnectionPhase::Middleware){phase=ConnectionPhase::Connected;changedAt=now;}}
  void cancel(){phase=ConnectionPhase::Idle;}
  ConnectionResult update(uint32_t now,bool associated){
    auto result=ConnectionResult::None;
    if(phase==ConnectionPhase::Wifi&&now-changedAt>=20000)result=ConnectionResult::WifiFailed;
    else if((phase==ConnectionPhase::Middleware||phase==ConnectionPhase::Connected)&&!associated)result=ConnectionResult::WifiFailed;
    else if(phase==ConnectionPhase::Middleware&&now-changedAt>=15000)result=ConnectionResult::MiddlewareFailed;
    else if(phase==ConnectionPhase::Connected&&now-changedAt>=1000)result=ConnectionResult::OpenSession;
    if(result!=ConnectionResult::None)cancel();
    return result;
  }
};
}
