#import <Foundation/Foundation.h>
#include "dyld_util.hpp"
#include "targeted_names.hpp"
#include <mutex>
#include <cstdarg>
static FILE *logFile=nullptr;
static std::mutex runLock;
static void emit(const char *fmt,...) {
 if(!logFile)return;
 va_list a;va_start(a,fmt);vfprintf(logFile,fmt,a);va_end(a);fputc('\n',logFile);fflush(logFile);
}
static bool expectedUUID(uintptr_t base) {
 const unsigned char expected[16]={0xbe,0xf9,0x0c,0x8f,0x5c,0xb6,0x3a,0x9c,0x81,0xc1,0x48,0x0e,0x7f,0xaf,0xaa,0xc4};
 const auto *mh=(const mach_header_64 *)base;
 if(!mh||mh->magic!=MH_MAGIC_64||mh->sizeofcmds>(1u<<20))return false;
 const unsigned char *p=(const unsigned char *)(mh+1),*end=p+mh->sizeofcmds;
 for(uint32_t k=0;k<mh->ncmds&&size_t(end-p)>=sizeof(load_command);++k) {
  const auto *c=(const load_command *)p;
  if(c->cmdsize<sizeof(*c)||c->cmdsize>size_t(end-p))return false;
  if(c->cmd==LC_UUID && c->cmdsize>=sizeof(uuid_command))return !memcmp(((const uuid_command *)p)->uuid,expected,16);
  p+=c->cmdsize;
 }
 return false;
}
extern "C" void zkProbe() {
 std::unique_lock<std::mutex> lock(runLock,std::try_to_lock);if(!lock.owns_lock())return;
 @autoreleasepool {
  NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
  NSString *path=[docs stringByAppendingPathComponent:@"zkprobe-targeted.txt"];
  logFile=fopen(path.UTF8String,"a");if(!logFile)return;
  struct Close {~Close(){fclose(logFile);logFile=nullptr;}} close;
  emit("[session] targeted-v1 time=%s",[NSDate.date.description UTF8String]);
  emit("[mode] bounded candidate reads; NO memory scan; NO game function calls");
  uintptr_t base=zk::mainBase();
  if(!expectedUUID(base)){emit("[stop] executable UUID mismatch; no candidate reads");return;}
  emit("[uuid] match bef90c8f5cb63a9c81c1480e7fafaac4");
  constexpr uintptr_t wrapperRVA=0x0a446b20;
  if(base>UINTPTR_MAX-wrapperRVA){emit("[stop] address overflow");return;}
  zk::resetScanBudget();
  emit("[stage] wrapper begin");
  auto result=zk::checkTargetedNames(base+wrapperRVA,[](uintptr_t a,void *p,size_t n){return zk::rd(a,p,n);});
  emit("[result] status=%s depth=%u decoded=%u",result.status,result.chainDepth,result.decoded);
  for(unsigned i=0;i<result.decoded;++i)emit("[name] id=%u text=%s",i,result.names[i]);
  const auto &b=zk::scanBudget();
  emit("[reads] bytes=%llu calls=%llu failures=%llu limit=%s",(unsigned long long)b.readBytes,(unsigned long long)b.readCalls,(unsigned long long)b.readFailures,b.reason);
  emit("[stage] complete; candidate only; do not copy to offsets.plist");
 }
}
__attribute__((constructor)) static void start() {
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,12ull*NSEC_PER_SEC),dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{zkProbe();});
}
