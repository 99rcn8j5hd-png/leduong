#include "../src/targeted_names.hpp"
#include <cassert>
#include <vector>
#include <cstring>
int main(){
 std::vector<unsigned char> m(0x20000,0);const uintptr_t base=0x10000;
 auto put=[&](uintptr_t a,uint64_t n){std::memcpy(m.data()+a-base,&n,8);};
 size_t bytes=0,calls=0;
 auto rd=[&](uintptr_t a,void *p,size_t n){++calls;bytes+=n;if(a<base||a-base>m.size()||n>m.size()-(a-base))return false;std::memcpy(p,m.data()+a-base,n);return true;};
 put(base,103);m[4]=1;put(base+8,0x11000);put(0x11000,0x12000);put(0x12000,0x13000);
 for(unsigned i=0;i<16;++i){put(0x13000+i*8,0x14000+i*128);const char *s=i?"TestName":"None";std::strcpy((char *)m.data()+0x14000+i*128-base+12,s);}
 auto r=zk::checkTargetedNames(base,rd);assert(r.decoded==16&&r.chainDepth==1);assert(bytes<4096&&calls<128);
 m[4]=0;assert(zk::checkTargetedNames(base,rd).decoded==0);m[4]=1;
 put(base,0xffffffff);assert(zk::checkTargetedNames(base,rd).decoded==0);
 put(base,148);put(base+8,0x11000);put(0x11000,0x11000);
 bytes=calls=0;r=zk::checkTargetedNames(base,rd);assert(r.decoded==0&&calls<128&&bytes<4096);
 put(base,103);put(0x11000,0x12000);m[0x4000+12]='X';r=zk::checkTargetedNames(base,rd);assert(!std::strcmp(r.status,"entry0_not_None"));
}
