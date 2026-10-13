#pragma once
#include <stdint.h>
namespace snowball {
constexpr uint16_t DefaultDoubleClickMs=250;
constexpr int SleepOptionCount=4,DoubleClickOptionCount=100;
inline uint8_t sleepMinutesAt(int index){return index==1?1:index==2?5:index==3?10:0;}
inline int sleepOption(uint32_t minutes){return minutes==1?1:minutes==5?2:minutes==10?3:0;}
inline uint16_t doubleClickAt(int index){return static_cast<uint16_t>((index+1)*5);}
struct DeviceSettings {
  uint8_t sleepMinutes=0;
  uint16_t doubleClickMs=DefaultDoubleClickMs;
  void load(uint32_t minutes,uint32_t clickMs){
    sleepMinutes=sleepMinutesAt(sleepOption(minutes));
    doubleClickMs=clickMs>=5&&clickMs<=500&&clickMs%5==0?clickMs:DefaultDoubleClickMs;
  }
};
// Screen-only sleep: networking and pending native receipts keep running.
class DisplayIdle {
public:
  uint32_t lastInput=0;
  bool sleeping=false;
  void sleep(){sleeping=true;}
  bool activity(uint32_t now){bool woke=sleeping;lastInput=now;sleeping=false;return woke;}
  bool update(uint32_t now,uint8_t minutes){
    if(!sleeping&&minutes&&now-lastInput>=static_cast<uint32_t>(minutes)*60000){sleep();return true;}
    return false;
  }
};
}
