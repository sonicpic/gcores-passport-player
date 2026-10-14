/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>
#include "storage/FlashCatalog.h"
struct FlashFixture {
    std::vector<unsigned char> bytes;
    size_t reads=0, writes=0, erases=0;
    size_t fail_write=SIZE_MAX, tear_bytes=0;
    bool fail_read=false, fail_erase=false;
    FlashFixture():bytes(gpp::FlashCatalog::kRequiredBytes,0xff) {}
    static bool read(void *ctx,size_t offset,void *out,size_t length) {
        auto& f=*static_cast<FlashFixture*>(ctx); ++f.reads;
        if (f.fail_read || offset>f.bytes.size() || length>f.bytes.size()-offset) return false;
        std::memcpy(out,f.bytes.data()+offset,length); return true;
    }
    static bool write(void *ctx,size_t offset,const void *in,size_t length) {
        auto& f=*static_cast<FlashFixture*>(ctx); const auto *p=static_cast<const unsigned char*>(in);
        const bool fail=++f.writes==f.fail_write;
        if (offset>f.bytes.size() || length>f.bytes.size()-offset) return false;
        const size_t n=fail?std::min(length,f.tear_bytes):length;
        for (size_t i=0;i<n;++i) { if ((f.bytes[offset+i]&p[i])!=p[i]) return false; f.bytes[offset+i]&=p[i]; }
        return !fail;
    }
    static bool erase(void *ctx,size_t offset,size_t length) {
        auto& f=*static_cast<FlashFixture*>(ctx); ++f.erases;
        if (f.fail_erase || offset%4096 || length%4096 || offset>f.bytes.size() || length>f.bytes.size()-offset) return false;
        std::fill(f.bytes.begin()+offset,f.bytes.begin()+offset+length,0xff); return true;
    }
    gpp::FlashCatalogIo io() { return {this,bytes.size(),read,write,erase}; }
};
inline gpp::FlashCatalogRecord flash_record(size_t index,const char *prefix="guid") {
    gpp::FlashCatalogRecord r{};
    const auto name=std::string(prefix)+std::to_string(index);
    std::strcpy(r.guid,name.c_str()); std::strcpy(r.title,name.c_str());
    std::strcpy(r.url,("https://gcores.com/"+name+".mp3").c_str());
    std::strcpy(r.chaptersUrl,("https://gcores.com/"+name+".json").c_str());
    gpp::FlashCatalog::put32(r.duration,3600+index);
    gpp::FlashCatalog::put64(r.enclosureLength,UINT64_C(0x1234567890)+index);
    gpp::FlashCatalog::seal(r); return r;
}
