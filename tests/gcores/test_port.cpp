/* SPDX-License-Identifier: GPL-3.0-only */
#include <cassert>
#include <string>
#include <cstdlib>
#include <new>
#include <algorithm>
#include <cstdio>
#include <cstddef>
#include "rss/TextUtils.h"
#include "rss/RssParser.h"
#include "timeline/TimelineParser.h"
#include "storage/CacheCodec.h"
#include "storage/ResumeValidation.h"
#include "catalog_metadata_under_test.h"
#include "catalog_api_under_test.h"
static size_t allocations;
static size_t live_bytes, peak_bytes;
struct alignas(std::max_align_t) AllocationHeader { size_t bytes; };
void* operator new(size_t bytes) {
    ++allocations;
    if (auto* memory=static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader)+(bytes?bytes:1)))) {
        memory->bytes=bytes;
        live_bytes+=bytes; peak_bytes=std::max(peak_bytes,live_bytes);
        return memory+1;
    }
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept {
    if (!memory) return;
    auto* header=static_cast<AllocationHeader*>(memory)-1;
    live_bytes-=header->bytes; std::free(header);
}
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory,size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory,size_t) noexcept { ::operator delete(memory); }

static void test_rss_large_item_memory_and_boundaries() {
    using namespace gpp;
    const size_t before_entities=live_bytes;
    const auto entities=text::decodeEntities("&amp;&lt;&gt;&quot;&apos;&nbsp;&ensp;&emsp;&ndash;&mdash;&middot;");
    assert(entities=="&<>\"'   –—·");
    assert(live_bytes-before_entities==entities.capacity()+1);
    std::printf("Entity table retained bytes: 0\n");
    assert(text::decodeEntities("&#65;&#x4E2D;&#X6587;&#x1F47E; &bogus; &#xD800; &#1114112; &amp") ==
           "A中文👾 &bogus; &#xD800; &#1114112; &amp");
    assert(text::xmlText(" \t<![CDATA[&amp;中文]]> \r\n",8)=="&中文");
    // Inputs are bounded borrowed slices, not necessarily NUL terminated.
    const std::string entity_source="PREFIX&#65;READ-BEYOND-VIEW";
    assert(text::decodeEntities(std::string_view(entity_source.data()+6,5))=="A");
    assert(text::decodeEntities("&#; &#+65; &#x; &not-an-entity; ") ==
           std::string(1,'\0')+" A &#x; &not-an-entity; ");
    const std::string description="<![CDATA[00:00 开场<br>00:05 第二章<br>"+std::string(11000,'x')+"]]>";
    std::string item="<item><title>节目 &amp; 标题</title><guid>large</guid><description>"+description+
        "</description><content:encoded><![CDATA[00:00 开场<br>00:05 第二章<br>"+std::string(10000,'y')+
        "]]></content:encoded><enclosure url='https://gcores.com/a?x=1&amp;y=2' type='audio/mpeg' length='123456'/>"
        "<itunes:duration>00:10</itunes:duration><podcast:chapters url='https://gcores.com/c.json'/></item>";
    item.insert(item.size()-7,24576-item.size(),' ');
    assert(item.size()==24576);
    const std::string xml="<rss><channel><title>Channel</title>"+item+
        "<item><title>Next</title><guid>next</guid></item></channel></rss>";
    size_t measured_peak=0;
    for (size_t chunk : {size_t(1),size_t(7),size_t(257),size_t(1024),xml.size()}) {
        const size_t start=live_bytes; peak_bytes=start;
        RssParser parser(5); parser.feed(xml.data(),0);
        for (size_t p=0;p<xml.size();p+=chunk) parser.feed(xml.data()+p,std::min(chunk,xml.size()-p));
        auto parsed=parser.finish();
        measured_peak=std::max(measured_peak,peak_bytes-start);
        assert(parsed.ok && !parsed.truncatedItem && parsed.channelTitle=="Channel");
        assert(parsed.episodes.size()==2 && parsed.episodes[0].title=="节目 & 标题");
        assert(parsed.episodes[1].title=="Next" && parsed.episodes[1].guid=="next");
        assert(parsed.episodes[0].guid=="large" && parsed.episodes[0].durationSeconds==10);
        assert(parsed.episodes[0].enclosureUrl=="https://gcores.com/a?x=1&y=2");
        assert(parsed.episodes[0].enclosureLength==123456);
        std::vector<TimelineEntry> chapters;
        assert(TimelineParser::unpack(parsed.episodes[0].timelinePacked,chapters));
        assert(chapters.size()==2 && chapters[1].title=="第二章" && chapters[1].startSeconds==5);
        assert(parsed.episodes[0].description.size()<=384);
    }
    std::printf("RSS exact-24-KiB item parsing peak: %zu bytes\n",measured_peak);
    // The old whole-item/field substrings peaked at 111,468 bytes in this
    // fixture. Keep headroom for C++ library differences without restoring it.
    assert(measured_peak<90000);
    const auto over=RssParser::parseDocument("<rss><channel>"+item.substr(0,item.size()-7)+
        " </item><item><title>After</title></item></channel></rss>",5,7);
    assert(over.ok && over.truncatedItem && over.episodes.size()==1 && over.episodes[0].title=="After");
}
struct MetadataSinkState { size_t count=0; };
static bool metadata_memory_sink(void *context,gpp::PodcastEpisode&& item) {
    auto& state=*static_cast<MetadataSinkState *>(context);
    ++state.count;
    assert(item.title=="节目" && item.guid=="id" && item.enclosureUrl=="https://gcores.com/a.mp3");
    assert(item.durationSeconds==10 && item.enclosureLength==123456);
    assert(item.chaptersUrl=="https://gcores.com/c.json");
    for (const auto *field : {&item.description,&item.contentEncoded,&item.author,&item.coverUrl,
                             &item.pubDate,&item.enclosureType,&item.chaptersType,&item.link,&item.timelinePacked})
        assert(field->empty());
    assert(item.timeline.empty());
    return true;
}
static void test_metadata_sink_bounded_memory() {
    // A maximum-size realistic item with large CDATA bodies must not allocate
    // either body while TLS is live, and 100 episodes retain no more than 5.
    std::string item="<item><title>节目</title><guid>id</guid><pubDate>Today</pubDate>"
        "<description><![CDATA[00:00 First<br>"+std::string(11000,'x')+"]]></description>"
        "<content:encoded><![CDATA[00:00 First<br>"+std::string(10000,'y')+"]]></content:encoded>"
        "<itunes:author>Author</itunes:author><itunes:image href='https://gcores.com/cover.png'/>"
        "<enclosure url='https://gcores.com/a.mp3' type='audio/mpeg' length='123456'/>"
        "<itunes:duration>00:10</itunes:duration><podcast:chapters url='https://gcores.com/c.json' type='application/json'/></item>";
    item.insert(item.size()-7,24576-item.size(),' ');
    size_t peaks[2]{},retained[2]{};
    for (size_t test=0;test<2;++test) {
        const size_t count=test?100:5;
        const size_t baseline=live_bytes; peak_bytes=baseline;
        {
            MetadataSinkState state;
            gpp::RssParser parser(count,false,metadata_memory_sink,&state,true,gpp::RssParseMode::CatalogMetadata);
            const size_t reserved=live_bytes-baseline;
            assert(reserved>=24576+1024 && reserved<27000);
            // Simulate a live TLS allocation after the parser is constructed.
            std::string tls(40000,'t');
            const size_t online=live_bytes; peak_bytes=online;
            parser.feed("<rss><channel>",14);
            for (size_t i=0;i<count;++i) for (size_t p=0;p<item.size();p+=1024)
                assert(parser.feed(item.data()+p,std::min(size_t(1024),item.size()-p)));
            auto result=parser.finish();
            assert(result.ok && result.emittedCount==count && result.episodes.empty() && state.count==count);
            peaks[test]=peak_bytes-online;
            retained[test]=live_bytes-online;
        }
        assert(live_bytes==baseline);
    }
    assert(peaks[0]<2048 && peaks[1]<2048 && retained[0]==retained[1]);
    std::printf("RSS metadata TLS-time extra peak 5/100: %zu/%zu bytes; retained: %zu/%zu bytes\n",
                peaks[0],peaks[1],retained[0],retained[1]);
}
static void test_catalog_current_and_identity() {
    using namespace gpp;
    namespace api=catalog_api_test;
    FlashFixture flash;
    assert(api::catalog.mount(flash.io()) && api::catalog.begin(0));
    for (size_t i=0;i<100;++i) assert(api::catalog.append(flash_record(i)));
    assert(api::catalog.commit());
    const auto before=allocations;
    passport_episode_t ep{};
    assert(api::passport_catalog_episode_count(0)==100);
    for (int i : {0,3,4,7,8,99}) {
        assert(api::passport_catalog_get_episode(0,i,&ep));
        assert(ep.duration_seconds==3600+unsigned(i));
    }
    assert(!api::passport_catalog_get_episode(0,100,&ep));
    assert(!api::passport_catalog_get_episode(5,0,&ep));
    assert(!api::passport_catalog_get_episode(0,0,nullptr));
    assert(api::passport_catalog_find_episode_by_identity(0,"guid99","https://gcores.com/guid99.mp3")==99);
    assert(api::passport_catalog_find_episode_by_identity(0,"guid99","https://gcores.com/guid98.mp3")==-1);
    assert(api::passport_catalog_set_playback(0,&ep)==ESP_OK);
    assert(api::playback_index==99);
    assert(api::passport_catalog_episode_count(0)==100);
    assert(allocations==before);
    api::timeline_packed=TimelineParser::pack({{0,"开场",{}},{100,"主题",{}}});
    std::strcpy(api::timeline_guid,ep.guid); std::strcpy(api::timeline_url,ep.url); api::timeline_channel=0;
    passport_chapter_t out{}; size_t index=99;
    auto allocated=allocations, reads=flash.reads;
    assert(api::passport_catalog_current_chapter_by_identity(0,ep.guid,ep.url,100,&out,&index));
    assert(out.start_seconds==100 && std::string_view(out.title)=="主题" && index==1);
    assert(api::passport_catalog_get_chapter_by_identity(0,ep.guid,ep.url,0,&out));
    assert(api::passport_catalog_chapter_count_by_identity(0,ep.guid,ep.url)==2);
    assert(!api::passport_catalog_current_chapter_by_identity(0,ep.guid,"wrong",100,&out,&index));
    assert(!api::passport_catalog_current_chapter_by_identity(0,ep.guid,ep.url,100,nullptr,&index));
    assert(api::passport_catalog_set_playback(0,&ep)==ESP_OK);
    assert(api::timeline_packed.size()>0 && flash.reads==reads && allocations==allocated);
    // Transaction refresh replaces rows while retained playback/timeline survive.
    assert(api::catalog.begin(0) && api::catalog.append(flash_record(0,"new")) && api::catalog.commit());
    api::page_channel=-1; api::playback_index=api::catalog.find(0,ep.guid,ep.url);
    allocated=allocations; reads=flash.reads;
    assert(api::passport_catalog_episode_count(0)==2);
    passport_episode_t pinned{};
    assert(api::passport_catalog_get_episode(0,1,&pinned));
    assert(!std::strcmp(pinned.guid,ep.guid) && !std::strcmp(pinned.url,ep.url));
    assert(api::passport_catalog_find_episode_by_identity(0,ep.guid,ep.url)==1);
    assert(api::passport_catalog_current_chapter_by_identity(0,ep.guid,ep.url,0,&out,nullptr));
    assert(allocations==allocated && flash.reads==reads);
    // A prepared timeline must survive pinning a different next source.
    assert(api::passport_catalog_get_episode(0,0,&ep));
    api::timeline_packed=TimelineParser::pack({{0,"Next",{}}});
    std::strcpy(api::timeline_guid,ep.guid); std::strcpy(api::timeline_url,ep.url);
    assert(api::passport_catalog_set_playback(0,&ep)==ESP_OK);
    assert(api::passport_catalog_episode_count(0)==1 && api::timeline_packed.size()>0);
}
static void test_current_packed() {
    using namespace gpp;
    const std::vector<TimelineEntry> entries={{10,"开场 👾",{}},{120,"话题与讨论",{}},{3600,"结尾",{}}};
    const auto packed=TimelineParser::pack(entries);
    PackedTimelineEntry selected{999,"unchanged",99};
    assert(!TimelineParser::currentPacked({},0,selected));
    assert(!TimelineParser::currentPacked(packed,9,selected));
    assert(selected.startSeconds==999 && selected.title=="unchanged" && selected.index==99);
    for (uint32_t position : {10u,11u,119u,120u,121u,3599u,3600u,UINT32_MAX}) {
        assert(TimelineParser::currentPacked(packed,position,selected));
        int expected=TimelineParser::currentIndex(entries,position);
        assert(selected.index==static_cast<size_t>(expected));
        assert(selected.startSeconds==entries[expected].startSeconds && selected.title==entries[expected].title);
        assert(selected.title.data()>=packed.data() && selected.title.data()+selected.title.size()<=packed.data()+packed.size());
    }
    // Reject a malformed later record even when the current first entry fits.
    for (size_t length=0;length<packed.size();++length)
        assert(!TimelineParser::currentPacked(packed.substr(0,length),10,selected));
    assert(!TimelineParser::currentPacked(packed+"trailing",10,selected));
    auto wrong_count=packed; wrong_count[0]=81;
    assert(!TimelineParser::currentPacked(wrong_count,10,selected));
    auto wrong_length=packed; wrong_length[6]=static_cast<char>(181); wrong_length[7]=0;
    assert(!TimelineParser::currentPacked(wrong_length,10,selected));
    const auto empty_title=TimelineParser::pack({{0,"",{}}});
    const auto embedded_null=TimelineParser::pack({{0,std::string("bad\0title",9),{}}});
    const auto unordered=TimelineParser::pack({{20,"Later",{}},{10,"Earlier",{}}});
    assert(!TimelineParser::currentPacked(empty_title,0,selected));
    assert(!TimelineParser::currentPacked(embedded_null,0,selected));
    assert(!TimelineParser::currentPacked(unordered,20,selected));
    const auto duplicate=TimelineParser::pack({{0,"First",{}},{0,"Last",{}}});
    assert(TimelineParser::currentPacked(duplicate,0,selected) && selected.index==1 && selected.title=="Last");
    std::vector<TimelineEntry> maximum;
    for (unsigned i=0;i<80;++i) maximum.push_back({i*30,std::string(180,'x'),{}});
    const auto max_packed=TimelineParser::pack(maximum);
    const auto before_query=allocations;
    assert(max_packed.size()==2+33*186 && max_packed.size()<=TimelineParser::kMaxPackedBytes);
    assert(TimelineParser::currentPacked(max_packed,UINT32_MAX,selected) && selected.index==32 && selected.title.size()==180);
    assert(allocations==before_query);
    std::vector<TimelineEntry> retained;
    assert(TimelineParser::unpack(max_packed,retained) && retained.size()==33);
    std::vector<TimelineEntry> small;
    for (unsigned i=0;i<100;++i) small.push_back({i*30,"Short",{}});
    const auto small_packed=TimelineParser::pack(small);
    assert(TimelineParser::currentPacked(small_packed,UINT32_MAX,selected) && selected.index==79);
    // An exact six-KiB boundary retains the whole final entry, never a fragment.
    maximum.resize(34); maximum[32].title.resize(176); maximum[33].title="xx";
    const auto exact=TimelineParser::pack(maximum);
    assert(exact.size()==TimelineParser::kMaxPackedBytes);
    assert(TimelineParser::currentPacked(exact,UINT32_MAX,selected) && selected.index==33 && selected.title=="xx");
    assert(!TimelineParser::currentPacked(exact+"x",0,selected));
    const auto four_byte=TimelineParser::pack({{0,std::string(177,'x')+"👾",{}}});
    assert(TimelineParser::currentPacked(four_byte,0,selected) && selected.title.size()==177);
}
int main() {
    using namespace gpp;
    test_rss_large_item_memory_and_boundaries();
    test_metadata_sink_bounded_memory();
    test_catalog_current_and_identity();
    test_current_packed();
    const std::string xml = "<rss><channel><title>Gcores</title>"
        "<item><guid>A</guid><title>甲乙丙</title><description><![CDATA[00:00 开场<br>00:05 话题]]></description></item>"
        "<item><guid>B</guid><title>第二集</title><description><![CDATA[00:00 开场]]></description></item></channel></rss>";
    RssParser lean(5, false); lean.feed(xml.data(), xml.size());
    auto result = lean.finish();
    assert(result.ok && result.episodes.size() == 2);
    assert(result.episodes[0].timelinePacked.empty() && result.episodes[1].timelinePacked.empty());
    RssParser selected(5); selected.setTimelineGuid("B"); selected.feed(xml.data(), xml.size());
    result = selected.finish();
    assert(result.episodes[0].timelinePacked.empty() && !result.episodes[1].timelinePacked.empty());
    RssParser fallback(5); fallback.setTimelineGuid("https://gcores.com/x.mp3");
    const std::string no_guid = "<item><title>Episode</title><description>00:00 Intro</description>"
        "<enclosure url=\"https://gcores.com/x.mp3\"/></item>";
    fallback.feed(no_guid.data(), no_guid.size());
    auto selected_fallback = fallback.finish();
    assert(!selected_fallback.episodes[0].timelinePacked.empty());
    std::string enormous = "<rss><channel><title>X</title><item><title>Oversized</title><description>" +
        std::string(1024 * 1024, 'x') + "</description></item><item><title>OK</title></item></channel></rss>";
    RssParser bounded(5); bounded.feed(enormous.data(), enormous.size());
    result = bounded.finish();
    assert(result.ok && result.truncatedItem && result.episodes.size() == 1 && result.episodes[0].title == "OK");
    auto invalid = TimelineParser::parsePodcastChaptersJson(
        R"({"chapters":[{"startTime":1e100,"title":"Bad"},{"startTime":1,"title":"Valid"}]})");
    assert(invalid.size() == 1 && invalid[0].startSeconds == 1);
    auto unicode = TimelineParser::parsePodcastChaptersJson(
        R"({"chapters":[{"startTime":0,"title":"\u5f00\u573a \ud83d\udc7e"}]})");
    assert(unicode.size() == 1 && unicode[0].title == "开场 👾");
    auto malformed = TimelineParser::parsePodcastChaptersJson(
        R"({"chapters":[{"startTime":0,"title":"\ud800"}]})");
    assert(malformed.empty());
    auto encoded = CacheCodec::encodeEpisodes(result.episodes);
    std::vector<PodcastEpisode> decoded;
    for (size_t i = 0; i < encoded.size(); ++i)
        assert(!CacheCodec::decodeEpisodes(encoded.substr(0, i), decoded, 5));
    assert(CacheCodec::decodeEpisodes(encoded, decoded, 5));
    const char guid[] = "saved-identity", url[] = "https://gcores.com/episode.mp3", title[] = "Episode";
    assert(validResumeIdentity(0, 255, 1, guid, sizeof(guid), url, sizeof(url), title, sizeof(title)));
    assert(validResumeIdentity(0, 99, 0, guid, sizeof(guid), url, sizeof(url), title, sizeof(title)));
    assert(validResumeIdentity(0, 100, 0, guid, sizeof(guid), url, sizeof(url), title, sizeof(title)));
    assert(!validResumeIdentity(0, 101, 0, guid, sizeof(guid), url, sizeof(url), title, sizeof(title)));
    const char http_url[]="http://alioss.gcores.com/example.mp3";
    assert(validResumeIdentity(0,255,0,guid,sizeof(guid),http_url,sizeof(http_url),title,sizeof(title)));
    for(const char *invalid : {"http://", "https://", "http:///bad", "ftp://example.org/a", "http://example.org/\r\n"})
        assert(!validResumeIdentity(0,255,0,guid,sizeof(guid),invalid,std::strlen(invalid)+1,title,sizeof(title)));

    assert(!validResumeIdentity(0, 255, 2, guid, sizeof(guid), url, sizeof(url), title, sizeof(title)));
    assert(!validResumeIdentity(5, 0, 0, guid, sizeof(guid), url, sizeof(url), title, sizeof(title)));
    assert(!validResumeIdentity(0, 0, 0, guid, sizeof(guid)-1, url, sizeof(url), title, sizeof(title)));
    const char injected[] = "https://gcores.com/\r\n";
    assert(!validResumeIdentity(0, 0, 0, guid, sizeof(guid), injected, sizeof(injected), title, sizeof(title)));
    PodcastEpisode metadata;
    metadata.title="节目"; metadata.guid="guid"; metadata.enclosureUrl="https://gcores.com/a.mp3";
    metadata.enclosureLength=123456; metadata.durationSeconds=3600; metadata.description="Summary";
    metadata.chaptersUrl="https://gcores.com/chapters.json";
    metadata.contentEncoded=metadata.link=metadata.author=metadata.coverUrl=metadata.pubDate=
        metadata.enclosureType=metadata.chaptersType=std::string(4096,'x');
    metadata.timeline.resize(80); metadata.timelinePacked="packed chapters";
    catalog_test::clear_extra_metadata(metadata,0);
    const size_t empty_capacity=std::string().capacity();
    for (const std::string *field : {&metadata.description,&metadata.contentEncoded,&metadata.link,&metadata.author,
        &metadata.coverUrl,&metadata.pubDate,&metadata.enclosureType,&metadata.chaptersType})
        assert(field->empty() && field->capacity()==empty_capacity);
    assert(metadata.timeline.empty() && metadata.timeline.capacity()==0);
    assert(metadata.title=="节目" && metadata.guid=="guid" && metadata.timelinePacked=="packed chapters");
    assert(metadata.enclosureLength==123456 && metadata.durationSeconds==3600);
    metadata.timelinePacked.clear(); // Refresh strips its time-axis before caching.
    PodcastEpisode legacy_lean;
    legacy_lean.channelId=metadata.channelId; legacy_lean.title=metadata.title; legacy_lean.guid=metadata.guid;
    legacy_lean.enclosureUrl=metadata.enclosureUrl; legacy_lean.enclosureLength=metadata.enclosureLength;
    legacy_lean.durationSeconds=metadata.durationSeconds; legacy_lean.description=metadata.description;
    legacy_lean.chaptersUrl=metadata.chaptersUrl;
    assert(CacheCodec::encodeEpisodes({metadata})==CacheCodec::encodeEpisodes({legacy_lean}));
    return 0;
}
