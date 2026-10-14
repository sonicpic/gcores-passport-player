/* SPDX-License-Identifier: GPL-3.0-only */
#include "FlashCatalog.h"
#include <cstring>

namespace gpp {
namespace {
constexpr uint8_t magic[8] = {'G','P','C','P','0','0','0','1'};
constexpr uint32_t marker = 0x51c0a7ed;
uint32_t crc(uint32_t value, const void *bytes, size_t length) {
  const auto *p = static_cast<const uint8_t *>(bytes);
  while (length--) {
    value ^= *p++;
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
  }
  return value;
}
bool newer(uint32_t a, uint32_t b) { return a != b && uint32_t(a-b) < 0x80000000u; }
template<size_t N> bool terminated(const char (&s)[N]) { return std::memchr(s, 0, N); }
}
uint32_t FlashCatalog::get32(const uint8_t *b) {
  return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
uint64_t FlashCatalog::get64(const uint8_t *b) { return get32(b) | uint64_t(get32(b+4)) << 32; }
void FlashCatalog::put32(uint8_t *b, uint32_t v) { for (unsigned i=0;i<4;++i) b[i]=uint8_t(v>>(i*8)); }
void FlashCatalog::put64(uint8_t *b, uint64_t v) { for (unsigned i=0;i<8;++i) b[i]=uint8_t(v>>(i*8)); }
void FlashCatalog::seal(FlashCatalogRecord& r) {
  put32(r.checksum,~crc(UINT32_MAX,&r,offsetof(FlashCatalogRecord,checksum)));
}
size_t FlashCatalog::offset(size_t c, size_t s) const { return (c*2+s)*kSlotBytes; }
bool FlashCatalog::validRecord(const FlashCatalogRecord& r) {
  return terminated(r.title) && r.title[0] && terminated(r.guid) && r.guid[0] &&
    terminated(r.url) && r.url[0] && terminated(r.chaptersUrl) &&
    get32(r.checksum) == ~crc(UINT32_MAX, &r, offsetof(FlashCatalogRecord, checksum));
}
bool FlashCatalog::validate(size_t c, size_t slot, State& state) const {
  uint8_t header[64];
  const size_t base=offset(c,slot);
  if (!io_.read(io_.context,base,header,sizeof(header)) || std::memcmp(header,magic,sizeof(magic)) ||
      get32(header+8)!=c || get32(header+20)!=sizeof(FlashCatalogRecord) ||
      get32(header+60)!=marker || get32(header+56)!=~crc(UINT32_MAX,header,56)) return false;
  const uint32_t count=get32(header+16);
  if (!count || count>kMaxEpisodes) return false;
  uint32_t sum=UINT32_MAX;
  FlashCatalogRecord record;
  for (size_t i=0;i<count;++i) {
    if (!io_.read(io_.context,base+kHeaderBytes+i*sizeof(record),&record,sizeof(record)) ||
        !validRecord(record)) return false;
    sum=crc(sum,&record,sizeof(record));
  }
  if (~sum!=get32(header+24)) return false;
  state.generation=get32(header+12); state.count=count; state.slot=slot;
  return true;
}
bool FlashCatalog::mount(const FlashCatalogIo& io) {
  abort(); available_=false; io_=io;
  for (auto& state:states_) state=State{};
  if (!io.read || !io.write || !io.erase || io.bytes<kRequiredBytes) return false;
  available_=true;
  for (size_t c=0;c<kChannels;++c) {
    State a,b;
    const bool va=validate(c,0,a), vb=validate(c,1,b);
    if (va) states_[c]=a;
    if (vb && (!va || newer(b.generation,a.generation))) states_[c]=b;
  }
  return true;
}
size_t FlashCatalog::count(size_t c) const { return c<kChannels ? states_[c].count : 0; }
bool FlashCatalog::read(size_t c, size_t i, FlashCatalogRecord& r) const {
  if (!available_ || c>=kChannels || i>=states_[c].count) return false;
  return io_.read(io_.context,offset(c,states_[c].slot)+kHeaderBytes+i*sizeof(r),&r,sizeof(r)) && validRecord(r);
}
int FlashCatalog::find(size_t c, const char *guid, const char *url) const {
  FlashCatalogRecord record;
  return find(c,guid,url,record);
}
int FlashCatalog::find(size_t c, const char *guid, const char *url, FlashCatalogRecord& record) const {
  if (!guid || !*guid || !url || !*url) return -1;
  for (size_t i=0;i<count(c);++i)
    if (read(c,i,record) && !std::strcmp(record.guid,guid) && !std::strcmp(record.url,url)) return int(i);
  return -1;
}
bool FlashCatalog::begin(size_t c) {
  if (!available_ || writing_ || c>=kChannels) return false;
  writingChannel_=c; writingSlot_=states_[c].slot==0 ? 1 : 0;
  written_=0; bodyCrc_=UINT32_MAX; failed_=false;
  if (!io_.erase(io_.context,offset(c,writingSlot_),kSlotBytes)) return false;
  writing_=true; return true;
}
bool FlashCatalog::append(const FlashCatalogRecord& r) {
  if (!writing_ || failed_ || written_>=kMaxEpisodes || !validRecord(r)) return false;
  if (!io_.write(io_.context,offset(writingChannel_,writingSlot_)+kHeaderBytes+written_*sizeof(r),&r,sizeof(r))) {
    failed_=true; return false;
  }
  bodyCrc_=crc(bodyCrc_,&r,sizeof(r)); ++written_; return true;
}
bool FlashCatalog::commit() {
  if (!writing_ || failed_ || !written_) return false;
  // Verify each persisted record before publishing the immutable header.
  uint32_t check=UINT32_MAX;
  FlashCatalogRecord record;
  const size_t base=offset(writingChannel_,writingSlot_);
  for (size_t i=0;i<written_;++i) {
    if (!io_.read(io_.context,base+kHeaderBytes+i*sizeof(record),&record,sizeof(record)) || !validRecord(record)) {
      failed_=true; return false;
    }
    check=crc(check,&record,sizeof(record));
  }
  if (check!=bodyCrc_) { failed_=true; return false; }
  uint8_t header[64]{};
  std::memcpy(header,magic,sizeof(magic)); put32(header+8,writingChannel_);
  put32(header+12,states_[writingChannel_].generation+1); put32(header+16,written_);
  put32(header+20,sizeof(record)); put32(header+24,~bodyCrc_);
  put32(header+56,~crc(UINT32_MAX,header,56)); put32(header+60,marker);
  if (!io_.write(io_.context,base,header,60) || !io_.write(io_.context,base+60,header+60,4)) {
    failed_=true; return false;
  }
  uint8_t persisted[64];
  if (!io_.read(io_.context,base,persisted,sizeof(persisted)) || std::memcmp(persisted,header,sizeof(header))) {
    failed_=true; return false;
  }
  State next; next.generation=get32(header+12); next.count=written_; next.slot=writingSlot_;
  states_[writingChannel_]=next; abort(); return true;
}
void FlashCatalog::abort() { writing_=false; failed_=false; written_=0; }
}
