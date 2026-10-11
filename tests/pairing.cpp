#include "../firmware/pairing.h"
#include <cassert>
#include <cstdio>
int main(){
  snowball::Pairings p;
  const std::string a="host_"+std::string(32,'a'),b="host_"+std::string(32,'b'),keyA(64,'a'),keyB(64,'b');
  assert(!p.selectedIdentityKnown());
  assert(p.migrateLegacy(keyA));assert(p.selected==0);
  assert(!p.selectedIdentityKnown()); // A retained single key is not a resolved PC destination.
  assert(p.enroll(b,keyB,"395")==1);assert(p.hosts[0].key==keyA);assert(p.selected==0);
  assert(p.enroll(a,keyA,"Original PC")==0);assert(p.count==2);
  assert(p.selectedIdentityKnown()); // Signed discovery may resolve the original key.
  assert(p.choose(1,false));assert(!p.choose(0,true));assert(p.selected==1);
  assert(p.enroll(a,keyB,"Spoof")<0);assert(p.hosts[0].key==keyA);
  assert(p.enroll("hostname",keyA,"Invalid")<0);
  assert(p.remove(0));assert(p.selected==0);assert(p.hosts[0].id==b);
  assert(p.remove(0));assert(p.selected==-1);
  assert(!p.selectedIdentityKnown());
  assert(p.stage(a,keyA,"Pending PC")==0);assert(!p.hosts[0].confirmed);assert(!p.choose(0,false));
  assert(p.stage(a,keyB,"Retry PC")==0);assert(p.hosts[0].key==keyB);
  p.hosts[0].confirmed=true;assert(p.choose(0,false));assert(p.stage(a,keyA,"Replace pinned PC")<0);
  assert(p.remove(0));assert(p.selected==-1);
  for(size_t i=0;i<snowball::Pairings::Capacity;i++){char h[38];snprintf(h,sizeof(h),"host_%032x",(unsigned)i);assert(p.enroll(h,keyA,"PC")==(int)i);}
  assert(p.enroll(a,keyA,"Overflow")<0);assert(p.count==16);
}
