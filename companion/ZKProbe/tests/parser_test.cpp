#include "../src/entry_parser.hpp"
#include <cassert>
#include <vector>
#include <limits>
int main() {
    std::string name; uint32_t next=0;
    const unsigned char a[]={8,0,'N','o','n','e'};
    assert(zk::parseEntry(a,sizeof(a),0,name,next) && name=="None" && next==6);
    assert(!zk::parseEntry(a,5,0,name,next));
    assert(!zk::parseEntry(a,0,0,name,next));
    assert(!zk::parseEntry(a,sizeof(a),UINT32_MAX,name,next));
    const unsigned char wide[]={3,0,'A',0};
    assert(zk::parseEntry(wide,sizeof(wide),0,name,next) && name=="A");
    std::vector<unsigned char> small(8192,0);
    small[8190]=8;
    assert(!zk::parseEntry(small.data(),small.size(),8190,name,next));
    const unsigned char oversized[]={0xff,0xff};
    assert(!zk::parseEntry(oversized,sizeof(oversized),0,name,next));
}
