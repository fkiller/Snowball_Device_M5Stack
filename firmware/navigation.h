#pragma once
#include <stdint.h>
#include <algorithm>
namespace snowball {
enum class Gesture { None, Click, Hold, Repeat, Double };
// Defer single-click admission until the double-click window closes. In
// particular, B double-click must never submit the first click's command.
class ButtonGesture {
  bool down=false,held=false,waiting=false,second=false;
  uint32_t pressedAt=0,releasedAt=0,repeatedAt=0;
public:
  Gesture update(uint32_t now,bool pressed){
    Gesture event=Gesture::None;
    if(waiting&&!down&&now-releasedAt>=250){waiting=false;event=Gesture::Click;}
    if(pressed&&!down){down=true;pressedAt=now;held=false;second=waiting;waiting=false;}
    else if(!pressed&&down){down=false;if(!held){if(second){event=Gesture::Double;second=false;}else{waiting=true;releasedAt=now;}}}
    if(down&&!held&&now-pressedAt>=500){held=true;waiting=false;second=false;repeatedAt=now;event=Gesture::Hold;}
    else if(down&&held&&now-repeatedAt>=180){repeatedAt=now;event=Gesture::Repeat;}
    return event;
  }
};
enum class Focus { Top, Content };
enum class Crossing { None, Top, Content };
struct Navigation {
  Focus focus=Focus::Content;
  int crumb=3,index=0,total=0,rows=7,returnCrumb=3;
  bool reader=true;
  int maximum()const{return std::max(0,total-(reader?rows:1));}
  void configure(int count,int visible,bool reading){total=std::max(0,count);rows=visible;reader=reading;index=std::max(0,std::min(index,maximum()));}
  void list(int count,int selected,int origin){focus=Focus::Content;returnCrumb=origin;index=selected;configure(count,7,false);}
  int start()const{return reader?index:std::max(0,std::min(index-rows/2,total-rows));}
  Crossing move(int direction,bool page=false){
    if(focus==Focus::Top){
      if(direction>0&&crumb==3){focus=Focus::Content;return Crossing::Content;}
      crumb=std::max(0,std::min(3,crumb+direction));return Crossing::None;
    }
    if(!page&&direction<0&&index==0){focus=Focus::Top;crumb=returnCrumb;return Crossing::Top;}
    index=std::max(0,std::min(maximum(),index+direction*(page?rows:1)));return Crossing::None;
  }
  void edge(bool end){if(focus==Focus::Top)crumb=end?3:0;else index=end?maximum():0;}
};
constexpr int ScreenWidth=320,ScreenHeight=240,TopHeight=30,BottomY=207,BottomHeight=33;
constexpr int BoxWidth=104,BoxStride=106;
}
