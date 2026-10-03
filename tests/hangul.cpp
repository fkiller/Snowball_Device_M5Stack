#include "../firmware/hangul.h"
#include "../firmware/submission.h"
#include <cassert>
#include <iostream>
int main(){
  using namespace snowball;
  for(const auto& state:{"idle","failed","cancelled","unknown","unconfirmed"})assert(!journalAdmitted(state));
  for(const auto& state:{"queued","dispatched","acknowledged","completed"})assert(journalAdmitted(state));
  assert(compose("gksrmf")==u8"한글");
  assert(compose("dkssudgktpdy")==u8"안녕하세요");
  assert(compose("rhk")==u8"과");assert(compose("rnlf")==u8"귈");
  assert(compose("rkqt")==u8"값");assert(compose("rkqtdl")==u8"값이");
  assert(compose("ekfr")==u8"닭");assert(compose("ekfrk")==u8"달가");
  assert(compose("Rk")==u8"까");assert(compose("dP")==u8"예");
  assert(compose("gksrmf 123!")==u8"한글 123!");
  TextInput t;t.toggle();for(char c:std::string("rkqt"))t.append(c);
  assert(t.text()==u8"값");t.backspace();assert(t.text()==u8"갑");t.backspace();assert(t.text()==u8"가");t.backspace();assert(t.text()==u8"ㄱ");t.backspace();assert(t.text().empty());
  for(char c:std::string("gksrmf"))t.append(c);t.toggle();for(char c:std::string(" WASD"))t.append(c);
  assert(t.text()==u8"한글 WASD");t.backspace();assert(t.text()==u8"한글 WAS");
  t.clear();t.toggle();for(char c:std::string("rhk"))t.append(c);t.backspace();assert(t.text()==u8"고");
  t.toggle();t.backspace();assert(t.text().empty());
  std::cout<<"Two-beolsik composition, splitting, backspace, and language toggle passed\n";
}
