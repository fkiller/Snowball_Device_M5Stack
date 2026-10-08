#pragma once
#include <array>
#include <string>
namespace snowball {
inline bool lowerHex(const std::string& s,size_t length){if(s.size()!=length)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
inline bool hostIdentity(const std::string& s){return s.size()==37&&s.substr(0,5)=="host_"&&lowerHex(s.substr(5),32);}
struct PairedHost {std::string id,key,name;};
struct Pairings {
  static constexpr size_t Capacity=16;
  std::array<PairedHost,Capacity> hosts{};
  size_t count=0;
  int selected=-1;
  int find(const std::string& id)const{for(size_t i=0;i<count;i++)if(hosts[i].id==id)return (int)i;return -1;}
  int enroll(const std::string& id,const std::string& key,const std::string& name){
    if(!hostIdentity(id)||!lowerHex(key,64)||name.size()>128)return -1;
    int i=find(id);
    // A changed key on an existing identity requires explicit removal first.
    if(i>=0){if(hosts[i].key!=key)return -1;hosts[i].name=name;return i;}
    // Migrate the old single-key enrollment only when that exact key is known.
    int legacy=find("");if(legacy>=0&&hosts[legacy].key==key)i=legacy;
    else {if(count==Capacity)return -1;i=(int)count++;}
    hosts[i]={id,key,name};return i;
  }
  bool migrateLegacy(const std::string& key){if(!lowerHex(key,64)||count)return false;hosts[count++]={"",key,"Previously paired PC"};selected=0;return true;}
  bool choose(int i,bool sending){if(sending||i<0||(size_t)i>=count)return false;selected=i;return true;}
  bool remove(int i){if(i<0||(size_t)i>=count)return false;for(size_t n=i;n+1<count;n++)hosts[n]=hosts[n+1];hosts[--count]={};if(selected==i)selected=-1;else if(selected>i)--selected;return true;}
};
}
