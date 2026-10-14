/* SPDX-License-Identifier: GPL-3.0-only */
#include <cassert>
#include <cstdio>
#include "flash_fixture.h"
#include "rss/RssParser.h"
using gpp::FlashCatalog;
static bool store_sink(void *ctx,gpp::PodcastEpisode&& item) {
    gpp::FlashCatalogRecord record{};
    std::strcpy(record.title,item.title.c_str()); std::strcpy(record.guid,item.guid.c_str());
    std::strcpy(record.url,item.enclosureUrl.c_str());
    FlashCatalog::seal(record);
    return static_cast<FlashCatalog*>(ctx)->append(record);
}
int main() {
    FlashFixture f; FlashCatalog catalog;
    assert(catalog.mount(f.io()) && catalog.count(0)==0);
    assert(!catalog.begin(5) && catalog.begin(0) && !catalog.begin(1));
    assert(!catalog.commit()); catalog.abort();
    for (size_t c=0;c<5;++c) {
        assert(catalog.begin(c));
        for (size_t i=0;i<100;++i) assert(catalog.append(flash_record(i)));
        assert(!catalog.append(flash_record(100)) && catalog.commit());
    }
    FlashCatalog mounted; assert(mounted.mount(f.io()));
    gpp::FlashCatalogRecord read;
    for (size_t c=0;c<5;++c) {
        assert(mounted.count(c)==100);
        for (size_t i: {size_t(0),size_t(3),size_t(4),size_t(7),size_t(8),size_t(99)}) {
            assert(mounted.read(c,i,read));
            assert(FlashCatalog::get32(read.duration)==3600+i);
            assert(FlashCatalog::get64(read.enclosureLength)==UINT64_C(0x1234567890)+i);
        }
        assert(!mounted.read(c,100,read));
    }
    assert(mounted.find(0,"guid99","https://gcores.com/guid99.mp3")==99);
    assert(mounted.find(0,"guid99","https://gcores.com/guid98.mp3")==-1);
    assert(mounted.find(5,"guid0","https://gcores.com/guid0.mp3")==-1);
    // Reset while streaming leaves the old committed A slot readable.
    assert(mounted.begin(0) && mounted.append(flash_record(0,"new")));
    FlashCatalog interrupted; assert(interrupted.mount(f.io()) && interrupted.count(0)==100);
    mounted.abort();
    // Inject a torn body write and both header publication writes separately.
    for (size_t stage=1;stage<=3;++stage) {
        FlashCatalog writer; assert(writer.mount(f.io()) && writer.begin(0));
        f.fail_write=f.writes+stage; f.tear_bytes=stage==3?2:7;
        const bool appended=writer.append(flash_record(0,"new"));
        if (stage==1) assert(!appended); else { assert(appended); assert(!writer.commit()); }
        writer.abort(); f.fail_write=SIZE_MAX;
        FlashCatalog recovered; assert(recovered.mount(f.io()) && recovered.count(0)==100);
        assert(recovered.read(0,0,read) && std::strcmp(read.guid,"guid0")==0);
    }
    assert(mounted.begin(0) && mounted.append(flash_record(0,"new")) && mounted.commit());
    FlashCatalog committed; assert(committed.mount(f.io()) && committed.count(0)==1);
    assert(committed.find(0,"new0","https://gcores.com/new0.mp3")==0);
    // Corrupt the latest B record: fall back to complete valid A snapshot.
    f.bytes[FlashCatalog::kSlotBytes+FlashCatalog::kHeaderBytes+10]^=1;
    FlashCatalog corrupt; assert(corrupt.mount(f.io()) && corrupt.count(0)==100);
    f.fail_erase=true; assert(!corrupt.begin(0)); f.fail_erase=false;
    f.fail_read=true; assert(!corrupt.read(0,0,read)); f.fail_read=false;
    auto small=f.io(); --small.bytes; FlashCatalog missing; assert(!missing.mount(small));
    // Streaming 100 playable items leaves no output vector of episodes.
    assert(corrupt.begin(0));
    std::string xml="<rss><channel><item><title>Unplayable</title></item>";
    for (size_t i=0;i<101;++i) {
        const auto id=std::to_string(i);
        xml+="<item><title>T"+id+"</title><guid>G"+id+"</guid><enclosure url='https://gcores.com/"+id+".mp3'/></item>";
    }
    xml+="</channel></rss>";
    gpp::RssParser parser(100,false,store_sink,&corrupt,true);
    for (size_t p=0;p<xml.size();p+=7) assert(parser.feed(xml.data()+p,std::min(size_t(7),xml.size()-p)));
    auto result=parser.finish();
    assert(result.ok && result.emittedCount==100 && result.episodes.empty() && parser.complete());
    assert(corrupt.commit() && corrupt.find(0,"G99","https://gcores.com/99.mp3")==99);
    assert(corrupt.find(0,"G100","https://gcores.com/100.mp3")==-1);
    assert(corrupt.begin(0)); f.fail_write=f.writes+1;
    gpp::RssParser failed(100,false,store_sink,&corrupt,true);
    assert(!failed.feed(xml.data(),xml.size()));
    assert(failed.finish().sinkFailed); corrupt.abort();
    assert(corrupt.count(0)==100);
    std::puts("PASS Flash catalog 5x100, boundaries, CRC rollback, torn writes and streaming sink");
}
