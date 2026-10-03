#include "../firmware/navigation.h"
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
  Navigation n;n.list(30,0,3);assert(n.start()==0);n.list(30,14,3);assert(n.start()==11);
  n.list(30,29,3);assert(n.start()==23);n.edge(false);assert(n.index==0);
  assert(n.move(-1)==Crossing::Top);assert(n.crumb==3);n.move(-1);assert(n.crumb==2);
  n.move(-1);assert(n.crumb==1);n.move(-1);assert(n.crumb==0);n.edge(true);
  assert(n.move(1)==Crossing::Content);n.list(30,29,3);n.move(-1,true);assert(n.index==22);
  n.configure(100,11,true);n.edge(true);assert(n.index==89);n.edge(false);n.move(-1,true);
  assert(n.focus==Focus::Content&&n.index==0);assert(n.move(-1)==Crossing::Top);
  ButtonGesture rollover;rollover.update(0xffffff00,true);assert(rollover.update(0x100,true)==Gesture::Hold);
  static_assert(2+2*BoxStride+BoxWidth<=ScreenWidth,"ABC boxes overflow");
  static_assert(BottomY+BottomHeight==ScreenHeight,"Footer exceeds LCD");
  std::cout<<"Navigation boundaries, anchors, paging, gesture admission and geometry passed\n";
}
