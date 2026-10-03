#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// Two-beolsik IME. Replaying the bounded keystroke history makes backspace
// undo one physical key, including compound vowels/finals and syllable splits.
namespace snowball {
inline std::string utf8(uint32_t cp) {
  std::string s;
  if(cp<128)s+=(char)cp;
  else if(cp<2048){s+=(char)(0xc0|(cp>>6));s+=(char)(0x80|(cp&63));}
  else {s+=(char)(0xe0|(cp>>12));s+=(char)(0x80|((cp>>6)&63));s+=(char)(0x80|(cp&63));}
  return s;
}
struct Jamo { int l,v,t; Jamo(int lead=-1,int vowel=-1,int tail=0):l(lead),v(vowel),t(tail){} };
inline Jamo key(char c) {
  switch(c){
    case 'r':return {0,-1,1};case 'R':return {1,-1,2};
    case 's':return {2,-1,4};case 'e':return {3,-1,7};case 'E':return {4,-1,0};
    case 'f':return {5,-1,8};case 'a':return {6,-1,16};case 'q':return {7,-1,17};case 'Q':return {8,-1,0};
    case 't':return {9,-1,19};case 'T':return {10,-1,20};case 'd':return {11,-1,21};case 'w':return {12,-1,22};case 'W':return {13,-1,0};
    case 'c':return {14,-1,23};case 'z':return {15,-1,24};case 'x':return {16,-1,25};case 'v':return {17,-1,26};case 'g':return {18,-1,27};
    case 'k':return {-1,0,0};case 'o':return {-1,1,0};case 'i':return {-1,2,0};case 'O':return {-1,3,0};case 'j':return {-1,4,0};case 'p':return {-1,5,0};case 'u':return {-1,6,0};case 'P':return {-1,7,0};
    case 'h':return {-1,8,0};case 'y':return {-1,12,0};case 'n':return {-1,13,0};case 'b':return {-1,17,0};case 'm':return {-1,18,0};case 'l':return {-1,20,0};
    default: if(c>='A'&&c<='Z')return key(c+32);return {};
  }
}
inline int vowel(int a,int b) {
  if(a==8&&b==0)return 9;if(a==8&&b==1)return 10;if(a==8&&b==20)return 11;
  if(a==13&&b==4)return 14;if(a==13&&b==5)return 15;if(a==13&&b==20)return 16;
  if(a==18&&b==20)return 19;if(a==9&&b==20)return 10;if(a==14&&b==20)return 15;
  return -1;
}
inline int finalPair(int a,int b) {
  if(a==1&&b==19)return 3;if(a==4&&b==22)return 5;if(a==4&&b==27)return 6;
  if(a==8){if(b==1)return 9;if(b==16)return 10;if(b==17)return 11;if(b==19)return 12;if(b==25)return 13;if(b==26)return 14;if(b==27)return 15;}
  if(a==17&&b==19)return 18;return 0;
}
inline int finalLead(int t){const int ls[]={-1,0,1,9,2,12,18,3,5,0,6,7,9,16,17,18,6,7,9,9,10,11,12,14,15,16,17,18};return ls[t];}
inline int finalKeep(int t){if(t==3)return 1;if(t==5||t==6)return 4;if(t>=9&&t<=15)return 8;if(t==18)return 17;return 0;}
inline std::string compose(const std::string& keys) {
  static const uint16_t initials[]={0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,0x3145,0x3146,0x3147,0x3148,0x3149,0x314a,0x314b,0x314c,0x314d,0x314e};
  int l=-1,v=-1,t=0;std::string out;
  auto flush=[&](){if(l>=0&&v>=0)out+=utf8(0xac00+(l*21+v)*28+t);else if(l>=0)out+=utf8(initials[l]);else if(v>=0)out+=utf8(0x314f+v);l=v=-1;t=0;};
  for(char c:keys){
    Jamo j=key(c);
    if(j.l<0&&j.v<0){flush();out+=c;continue;}
    if(j.v>=0){
      if(v<0){v=j.v;continue;}
      if(t){int moved=finalLead(t),keep=finalKeep(t);t=keep;flush();l=moved;v=j.v;continue;}
      int joined=vowel(v,j.v);if(joined>=0){v=joined;continue;}
      flush();v=j.v;
    } else {
      if(l>=0&&v>=0&&j.t){if(!t){t=j.t;continue;}int joined=finalPair(t,j.t);if(joined){t=joined;continue;}}
      flush();l=j.l;
    }
  }
  flush();return out;
}
class TextInput {
  std::vector<std::string> parts;
  std::string keys;
 public:
  bool korean=false;
  std::string text()const{std::string out;for(const auto& p:parts)out+=p;return out+compose(keys);}
  void commit(){auto s=compose(keys);if(!s.empty())parts.push_back(s);keys.clear();}
  void toggle(){commit();korean=!korean;}
  bool append(char c){if(text().size()>=2000)return false;if(korean)keys+=c;else parts.push_back(std::string(1,c));return true;}
  void backspace(){if(!keys.empty()){keys.pop_back();return;}if(parts.empty())return;auto &s=parts.back();size_t i=s.size()-1;while(i>0&&((unsigned char)s[i]&0xc0)==0x80)--i;s.erase(i);if(s.empty())parts.pop_back();}
  void clear(){parts.clear();keys.clear();}
};
}
