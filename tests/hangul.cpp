#include "../firmware/hangul.h"
#include "../firmware/submission.h"
#include "../firmware/editor_layout.h"
#include <cassert>
#include <iostream>
int main(){
  using namespace snowball;
  for(const auto& state:{"idle","failed","cancelled","unknown","unconfirmed"})assert(!journalAdmitted(state));
  for(const auto& state:{"queued","dispatched","acknowledged","completed"})assert(journalAdmitted(state));
  assert(!ownJournalAdmission("mine","cmd_m5_other","completed",true));assert(!ownJournalAdmission("mine","cmd_m5_mine","unknown",true));
  assert(!ownJournalAdmission("mine","cmd_m5_mine","queued",false));assert(ownJournalAdmission("mine","cmd_m5_mine","queued",true));
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
  t.clear();t.korean=true;for(char c:std::string("gksrmf"))t.append(c);t.move(-1);assert(t.caret()==3);
  t.toggle();t.append('X');assert(t.text()==u8"한X글");t.backspace();assert(t.text()==u8"한글");
  t.edge(false);t.append('A');assert(t.text()==u8"A한글");t.edge(true);t.append('Z');assert(t.text()==u8"A한글Z");
  t.move(-1);t.backspace();assert(t.text()==u8"A한Z");t.korean=true;for(char c:std::string("rhk"))t.append(c);assert(t.text()==u8"A한과Z");
  auto layout=editorLayout(t.text(),t.caret(),2,[](const std::string& g){return g.size()==1?1:2;});
  assert(layout.rows.size()==4);assert(layout.cursorRow==3);assert(layout.rows[2].end-layout.rows[2].begin==3);
  auto newline=editorLayout("ab\ncd",3,20,[](const std::string&){return 1;});assert(newline.cursorRow==1);
  std::cout<<"Two-beolsik composition, splitting, backspace, and language toggle passed\n";
}
