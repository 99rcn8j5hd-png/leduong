#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace zk {
struct TargetedResult {
 const char *status="not_checked";
 uint32_t chainDepth=0;
 uintptr_t table=0;
 unsigned decoded=0;
 char names[16][65]{};
};
inline bool candidatePointer(uintptr_t p) { return p>=0x10000 && (p&7)==0 && p<=UINTPTR_MAX-128; }
// Model inferred from one executable; read callback must have its own hard read budget.
template<class Read> TargetedResult checkTargetedNames(uintptr_t wrapper, Read read) {
 TargetedResult r;
 if(!candidatePointer(wrapper)) { r.status="bad_wrapper_address"; return r; }
 unsigned char h[16]{};
 if(!read(wrapper,h,sizeof(h))) { r.status="wrapper_read_failed"; return r; }
 uint32_t tag; uintptr_t p;
 std::memcpy(&tag,h,4); std::memcpy(&p,h+8,8);
 if(!(h[4]&1)) { r.status="wrapper_not_initialized"; return r; }
 if(tag<103 || tag>148 || (tag-100)%3) { r.status="unexpected_chain_tag"; return r; }
 r.chainDepth=(tag-100)/3;
 if(!candidatePointer(p)) { r.status="invalid_chain_pointer"; return r; }
 for(uint32_t j=1;j<r.chainDepth;++j) {
  uintptr_t next=0;
  if(!read(p,&next,8) || !candidatePointer(next)) { r.status="chain_read_failed"; return r; }
  p=next;
 }
 uintptr_t table=0;
 if(!read(p,&table,8) || !candidatePointer(table)) { r.status="table_read_failed"; return r; }
 r.table=table;
 uintptr_t chunk=0;
 if(!read(table,&chunk,8) || !candidatePointer(chunk)) { r.status="chunk0_read_failed"; return r; }
 for(unsigned id=0;id<16;++id) {
  uintptr_t entry=0;
  if(!read(chunk+id*8,&entry,8) || !candidatePointer(entry)) { r.status="entry_read_failed"; return r; }
  uint32_t flags=0;
  if(!read(entry+8,&flags,4)) { r.status="entry_flags_read_failed"; return r; }
  if(flags&1) { r.status="wide_entry_not_supported"; return r; }
  unsigned char text[64]{};
  if(!read(entry+12,text,sizeof(text))) { r.status="entry_text_read_failed"; return r; }
  size_t n=0;
  for(;n<sizeof(text)&&text[n];++n) {
   if(text[n]<32||text[n]>126) { r.status="non_ascii_name"; return r; }
  }
  if(!n||n==sizeof(text)) { r.status="missing_name_terminator"; return r; }
  std::memcpy(r.names[id],text,n); ++r.decoded;
  if(id==0 && std::strcmp(r.names[0],"None")) { r.status="entry0_not_None"; return r; }
 }
 r.status="candidate_decoded_16_names_NOT_runtime_verified";
 return r;
}
}
