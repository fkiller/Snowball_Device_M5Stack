#include "../firmware/navigation.h"
#include "../firmware/device_settings.h"
#include <cassert>
#include <iostream>
int main(){
  using namespace snowball;
  ButtonGesture single;assert(single.update(1,true)==Gesture::None);assert(single.update(60,false)==Gesture::None);
  assert(single.update(309,false)==Gesture::None);assert(single.update(310,false)==Gesture::Click);
  ButtonGesture twice;twice.update(10,true);twice.update(70,false);twice.update(100,true);
  assert(twice.update(170,false)==Gesture::Double);assert(twice.update(900,false)==Gesture::None);
  ButtonGesture hold;hold.update(100,true);assert(hold.update(600,true)==Gesture::Hold);
  assert(hold.update(779,true)==Gesture::None);assert(hold.update(780,true)==Gesture::Repeat);
  assert(hold.update(800,false)==Gesture::None);assert(hold.update(1200,false)==Gesture::None);
  // A long second press cannot accidentally admit the pending first B click.
  ButtonGesture secondHold;secondHold.update(10,true);secondHold.update(60,false);secondHold.update(100,true);
  assert(secondHold.update(400,true)==Gesture::None);assert(secondHold.update(600,true)==Gesture::Hold);
  for(uint16_t window:{5,250,500}){
    ButtonGesture click;click.update(1,true,window);click.update(2,false,window);
    assert(click.update(1+window,false,window)==Gesture::None);
    assert(click.update(2+window,false,window)==Gesture::Click);
    assert(click.update(3+window,false,window)==Gesture::None);
    ButtonGesture dbl;dbl.update(1,true,window);dbl.update(2,false,window);
    assert(dbl.update(1+window,true,window)==Gesture::None);
    assert(dbl.update(2+window,false,window)==Gesture::Double);
    assert(dbl.update(1000,false,window)==Gesture::None);
    // At the deadline two separate clicks remain separate.
    ButtonGesture late;late.update(1,true,window);late.update(2,false,window);
    assert(late.update(2+window,true,window)==Gesture::Click);
    assert(late.update(3+window,false,window)==Gesture::None);
    assert(late.update(3+2*window,false,window)==Gesture::Click);
    ButtonGesture held;held.update(1,true,window);assert(held.update(501,true,window)==Gesture::Hold);
    assert(held.update(681,true,window)==Gesture::Repeat);held.update(682,false,window);
    assert(held.update(1300,false,window)==Gesture::None);
  }
  ButtonGesture stable;stable.update(1,true,500);stable.update(2,false,500);
  assert(stable.update(7,false,5)==Gesture::None); // pending click retains its admitted window
  assert(stable.update(502,false,5)==Gesture::Click);
  ButtonGesture wrapped;wrapped.update(0xfffffff0,true,5);wrapped.update(0xfffffffe,false,5);
  assert(wrapped.update(2,false,5)==Gesture::None);assert(wrapped.update(3,false,5)==Gesture::Click);
  DeviceSettings stored;stored.load(5,500);assert(stored.sleepMinutes==5&&stored.doubleClickMs==500);
  stored.load(2,501);assert(stored.sleepMinutes==0&&stored.doubleClickMs==250);
  for(uint32_t invalid:{0u,4u,6u,499u,0xffffffffu}){stored.load(0,invalid);assert(stored.doubleClickMs==250);}
  for(int i=0;i<SleepOptionCount;i++)assert(sleepOption(sleepMinutesAt(i))==i);
  for(int i=0;i<DoubleClickOptionCount;i++){stored.load(10,doubleClickAt(i));assert(stored.doubleClickMs==doubleClickAt(i));}
  DisplayIdle idle;idle.activity(1);assert(!idle.update(1000000,0)); // Off
  // Explicit sleep works with the idle timer Off and survives background ticks.
  idle.sleep();assert(idle.sleeping&&idle.lastInput==1);
  assert(!idle.update(1000001,0)&&idle.sleeping);
  assert(!idle.update(1000002,1)&&idle.sleeping);
  assert(idle.activity(1000003)&&!idle.sleeping);
  assert(!idle.activity(1000004));assert(!idle.update(1060003,1));
  for(uint8_t minutes:{1,5,10}){
    idle.activity(100);assert(!idle.update(99+minutes*60000,minutes));
    assert(idle.update(100+minutes*60000,minutes));assert(idle.sleeping);
    assert(!idle.update(101+minutes*60000,minutes)); // background ticks never wake it
    assert(idle.activity(102+minutes*60000));assert(!idle.sleeping);
  }
  idle.activity(100);idle.activity(60099);assert(!idle.update(60100,1));
  idle.activity(0xfffffff0);assert(!idle.update(0x20,1));assert(idle.update(59984,1));
  Navigation n;n.list(30,0,4);assert(n.start()==0);n.list(30,14,4);assert(n.start()==11);
  n.list(30,29,4);assert(n.start()==23);n.edge(false);assert(n.index==0);
  assert(n.move(-1)==Crossing::Top);assert(n.crumb==4);n.move(-1);assert(n.crumb==3);n.move(-1);assert(n.crumb==2);
  n.move(-1);assert(n.crumb==1);n.move(-1);assert(n.crumb==0);n.edge(true);
  assert(n.move(1)==Crossing::Content);n.list(30,29,4);n.move(-1,true);assert(n.index==22);
  n.configure(100,11,true);n.edge(true);assert(n.index==89);assert(n.move(1)==Crossing::Editor);assert(n.move(1,true)==Crossing::Editor);n.edge(false);n.move(-1,true);
  assert(n.focus==Focus::Content&&n.index==0);assert(n.move(-1)==Crossing::Top);
  n.focus=Focus::Content;n.configure(0,11,true);assert(n.move(1)==Crossing::Editor);
  ButtonGesture rollover;rollover.update(0xffffff00,true);assert(rollover.update(0x100,true)==Gesture::Hold);
  static_assert(2+2*BoxStride+BoxWidth<=ScreenWidth,"ABC boxes overflow");
  static_assert(BottomY+BottomHeight==ScreenHeight,"Footer exceeds LCD");
  static_assert(ContentTextY+ContentRows*ContentLineHeight<BottomY,"Content overlaps footer");
  ReaderWindow current{24,24,12},down{24,25,12},up{24,23,12},pageAhead{24,36,12};
  for(int row=0;row<12;row++)assert(current.source(row)==row);
  assert(down.source(0)==1&&down.source(10)==11&&down.source(11)==-1);
  assert(up.source(0)==-1&&up.source(1)==0&&up.source(11)==10);
  for(int row=0;row<12;row++)assert(pageAhead.source(row)==-1);
  assert(current.source(-1)==-1&&current.source(12)==-1);
  MenuHistory history;Navigation settings;settings.list(6,4,0);
  assert(history.remember(8,settings));Navigation language;language.list(2,1,0);
  int parent=-1;assert(history.restore(13,parent,language));assert(parent==8&&language.index==4&&language.focus==Focus::Content);
  settings.list(6,0,0);history.remember(8,settings);Navigation aps;aps.list(27,19,0);history.remember(4,aps);
  Navigation password;password.list(3,2,0);assert(history.restore(5,parent,password));assert(parent==4&&password.index==19&&password.start()==16);
  assert(history.restore(4,parent,password));assert(parent==8&&password.index==0);
  // A repeated middleware/Wi-Fi failure path retires old descendants.
  history.remember(8,settings);Navigation find;find.list(3,1,0);history.remember(11,find);history.remember(4,aps);
  find.index=0;history.remember(11,find);assert(history.restore(10,parent,password)&&parent==11&&password.index==0);
  assert(history.restore(11,parent,password)&&parent==8);assert(!history.restore(8,parent,password));
  history.remember(8,settings);history.clear();assert(!history.restore(13,parent,password));
  for(int setting:{6,7}){
    settings.list(9,setting,0);assert(settings.start()==2);
    history.remember(8,settings);Navigation choice;choice.list(setting==6?SleepOptionCount:DoubleClickOptionCount,0,0);
    choice.edge(true);assert(choice.index==(setting==6?3:99));
    assert(history.restore(setting==6?17:18,parent,choice));
    assert(parent==8&&choice.index==setting&&choice.total==9&&choice.start()==2);
  }
  settings.list(9,8,0);assert(settings.start()==2);
  idle.sleep();assert(settings.index==8&&settings.total==9);
  assert(idle.activity(2000000));assert(settings.index==8&&settings.start()==2);
  for(int total:{0,1,12,13,100,100000}){
    int maximum=std::max(0,total-ContentRows);auto first=scrollbar(total,ContentRows,0,175),last=scrollbar(total,ContentRows,maximum,175);
    assert(first.top==0&&first.height>0&&first.height<=175);
    assert(last.top+last.height==175);
    int previous=0;for(int offset=0;offset<=maximum;offset++){auto bar=scrollbar(total,ContentRows,offset,175);assert(bar.top>=previous&&bar.top+bar.height<=175);previous=bar.top;}
    assert(scrollbar(total,ContentRows,-1,175).top==0);
    assert(scrollbar(total,ContentRows,1000000,175).top==last.top);
  }
  std::cout<<"Navigation boundaries, anchors, paging, gesture admission and geometry passed\n";
}
