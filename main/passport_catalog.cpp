/* SPDX-License-Identifier: GPL-3.0-only */
#include "passport_catalog.h"
#include "gcores/rss/RssParser.h"
#include "gcores/storage/CacheCodec.h"
#include "gcores/storage/FlashCatalog.h"
#include "gcores/storage/ResumeValidation.h"
#include "gcores/timeline/TimelineParser.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

namespace {
constexpr size_t kMaxCache=16384, kMaxFeedBytes=2*1024*1024, kMaxChapterBytes=32768;
constexpr const char *kTag="catalog";
const gpp::PodcastChannel kChannels[] = {
    {"games", "机核游戏频道", "游戏", "GADIO", "https://alioss.gcores.com/feeds/apple_podcast_v1.xml"},
    {"life", "机核生活频道", "生活", "Gadio Life", "https://alioss.gcores.com/feeds/gadio_life.xml"},
    {"pro", "机核兴趣频道", "兴趣", "Gadio Pro", "https://alioss.gcores.com/feeds/gadio_pro.xml"},
    {"heshi", "核市奇谭", "核市", "核市奇谭", "https://alioss.gcores.com/feeds/heshi.xml"},
    {"yinping", "银屏系漫游指南", "银屏", "银屏系漫游指南", "https://alioss.gcores.com/feeds/yin_ping.xml"},
};
SemaphoreHandle_t mutex;
nvs_handle_t storage;
bool storage_open;
gpp::FlashCatalog catalog;
/* One wire-record workspace shared only under Lock (or before tasks start).
 * Keeps the 6-KiB application stack independent of the on-Flash record width. */
gpp::FlashCatalogRecord io_record;
const esp_partition_t *cache_partition;
std::array<passport_episode_t,PASSPORT_EPISODE_PAGE_SIZE> page;
int page_channel=-1, page_start=0, page_count=0;
passport_episode_t playback{};
char playback_chapters[1025]{};
int playback_channel=-1, playback_index=-1;
std::string timeline_packed;
char timeline_guid[257]{}, timeline_url[1025]{};
int timeline_channel=-1;
passport_resume_t resume{};
bool have_resume;
struct Lock {
    Lock() { if (mutex) xSemaphoreTake(mutex,portMAX_DELAY); }
    ~Lock() { if (mutex) xSemaphoreGive(mutex); }
};
bool valid_channel(int c) { return c>=0 && c<PASSPORT_CHANNEL_COUNT; }
bool network_memory_available() {
    // Admit before C++ reserve and TLS allocations: exceptions are disabled
    // on target, so a fragmented heap must yield a recoverable error instead.
    return esp_get_free_heap_size()>=80*1024 &&
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)>=32*1024;
}
bool valid_resume(const passport_resume_t& value) {
    const auto *raw=reinterpret_cast<const unsigned char *>(&value);
    return gpp::validResumeIdentity(value.channel,value.episode,raw[offsetof(passport_resume_t,completed)],
        value.guid,sizeof(value.guid),value.url,sizeof(value.url),value.title,sizeof(value.title));
}
template<size_t N> void copy(char (&out)[N], const std::string& in) {
    size_t n=std::min(in.size(),N-1);
    while (n && n<in.size() && (static_cast<unsigned char>(in[n])&0xc0)==0x80) --n;
    memcpy(out,in.data(),n); out[n]=0;
}
void clear_extra_metadata(gpp::PodcastEpisode& e,int c) {
    e.channelId=kChannels[c].id;
    std::string().swap(e.description); std::string().swap(e.contentEncoded);
    std::vector<gpp::TimelineEntry>().swap(e.timeline);
    std::string().swap(e.link); std::string().swap(e.author);
    std::string().swap(e.coverUrl); std::string().swap(e.pubDate);
    std::string().swap(e.enclosureType); std::string().swap(e.chaptersType);
}
bool flash_read(void *context,size_t offset,void *out,size_t bytes) {
    return esp_partition_read(static_cast<const esp_partition_t *>(context),offset,out,bytes)==ESP_OK;
}
bool flash_write(void *context,size_t offset,const void *in,size_t bytes) {
    return esp_partition_write(static_cast<const esp_partition_t *>(context),offset,in,bytes)==ESP_OK;
}
bool flash_erase(void *context,size_t offset,size_t bytes) {
    return esp_partition_erase_range(static_cast<const esp_partition_t *>(context),offset,bytes)==ESP_OK;
}
void to_record(const gpp::PodcastEpisode& e,gpp::FlashCatalogRecord& record) {
    memset(&record,0,sizeof(record));
    copy(record.title,e.title); copy(record.guid,e.guid); copy(record.url,e.enclosureUrl);
    copy(record.chaptersUrl,e.chaptersUrl);
    gpp::FlashCatalog::put32(record.duration,e.durationSeconds);
    gpp::FlashCatalog::put64(record.enclosureLength,e.enclosureLength);
    gpp::FlashCatalog::seal(record);
}
void from_record(const gpp::FlashCatalogRecord& record,passport_episode_t& out) {
    memset(&out,0,sizeof(out));
    memcpy(out.title,record.title,sizeof(out.title)); memcpy(out.guid,record.guid,sizeof(out.guid));
    memcpy(out.url,record.url,sizeof(out.url));
    out.duration_seconds=gpp::FlashCatalog::get32(record.duration);
    out.enclosure_length=gpp::FlashCatalog::get64(record.enclosureLength);
}
bool valid_snapshot(const passport_episode_t *snapshot) {
    return snapshot && memchr(snapshot->title,0,sizeof(snapshot->title)) && snapshot->title[0] &&
        memchr(snapshot->guid,0,sizeof(snapshot->guid)) && snapshot->guid[0] &&
        memchr(snapshot->url,0,sizeof(snapshot->url)) && snapshot->url[0];
}
bool same_identity(const passport_episode_t& a,const char *guid,const char *url) {
    return guid && url && !strcmp(a.guid,guid) && !strcmp(a.url,url);
}
int find_locked(int c,const char *guid,const char *url) {
    return catalog.find(c,guid,url,io_record);
}
size_t visible_count(int c) {
    return catalog.count(c)+(playback_channel==c && playback_index<0 ? 1 : 0);
}
bool valid_episode(int c,int e) {
    return valid_channel(c) && e>=0 && size_t(e)<visible_count(c);
}
bool get_episode_locked(int c,int e,passport_episode_t *out) {
    if (!out || !valid_episode(c,e)) return false;
    if (playback_channel==c && playback_index<0 && size_t(e)==catalog.count(c)) { *out=playback; return true; }
    const int start=e/PASSPORT_EPISODE_PAGE_SIZE*PASSPORT_EPISODE_PAGE_SIZE;
    if (page_channel!=c || page_start!=start) {
        page_channel=-1; page_count=0;
        for (size_t i=start;i<std::min(catalog.count(c),size_t(start+PASSPORT_EPISODE_PAGE_SIZE));++i) {
            if (!catalog.read(c,i,io_record)) return false;
            from_record(io_record,page[page_count++]);
        }
        page_start=start; page_channel=c;
    }
    if (e-start>=page_count) return false;
    *out=page[e-start]; return true;
}
bool timeline_matches(int c,const char *guid,const char *url) {
    return valid_channel(c) && timeline_channel==c && guid && url && *guid && *url &&
        !strcmp(timeline_guid,guid) && !strcmp(timeline_url,url);
}
size_t packed_count() {
    gpp::PackedTimelineEntry entry;
    if (!gpp::TimelineParser::currentPacked(timeline_packed,UINT32_MAX,entry)) return 0;
    return static_cast<unsigned char>(timeline_packed[0]) |
        (size_t(static_cast<unsigned char>(timeline_packed[1]))<<8);
}
bool packed_chapter(size_t index,passport_chapter_t *out) {
    if (!out || index>=packed_count()) return false;
    size_t cursor=2;
    for (size_t i=0;i<=index;++i) {
        const uint32_t seconds=gpp::FlashCatalog::get32(reinterpret_cast<const uint8_t *>(timeline_packed.data()+cursor));
        cursor+=4;
        const size_t length=static_cast<unsigned char>(timeline_packed[cursor]) |
            (size_t(static_cast<unsigned char>(timeline_packed[cursor+1]))<<8);
        cursor+=2;
        if (i==index) { out->start_seconds=seconds; memcpy(out->title,timeline_packed.data()+cursor,length); out->title[length]=0; return true; }
        cursor+=length;
    }
    return false;
}
struct HttpContext {
    gpp::RssParser *parser = nullptr;
    std::string *body = nullptr;
    size_t bytes = 0;
    size_t limit = kMaxFeedBytes;
    std::string redirect;
    bool *stop = nullptr;
};
esp_err_t on_http(esp_http_client_event_t *event) {
    auto *ctx = static_cast<HttpContext *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        strcasecmp(event->header_key, "Location") == 0 && event->header_value &&
        strlen(event->header_value) <= 1024) ctx->redirect = event->header_value;
    return ESP_OK;
}
esp_err_t request(const char *url, HttpContext &ctx) {
    if (!url || strncmp(url, "https://", 8) != 0) return ESP_ERR_INVALID_ARG;
    esp_http_client_config_t config{};
    config.url = url; config.timeout_ms = 18000;
    config.buffer_size = 1024; config.buffer_size_tx = 512;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = on_http; config.user_data = &ctx;
    config.max_redirection_count = 3;
    config.disable_auto_redirect = true;
    config.user_agent = "GcoresPassport/1.0";
    auto client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t result = ESP_FAIL;
    for (unsigned redirects = 0; redirects <= 3; ++redirects) {
        ctx.redirect.clear();
        result = esp_http_client_open(client, 0);
        if (result != ESP_OK) break;
        if (esp_http_client_fetch_headers(client) < 0) { result = ESP_FAIL; break; }
        const int status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (redirects == 3 || ctx.redirect.compare(0, 8, "https://") != 0) { result = ESP_ERR_INVALID_RESPONSE; break; }
            esp_http_client_close(client);
            result = esp_http_client_set_url(client, ctx.redirect.c_str());
            if (result != ESP_OK) break;
            continue;
        }
        if (status != 200) { result = ESP_ERR_INVALID_RESPONSE; break; }
        char chunk[1024];
        result = ESP_OK;
        for (;;) {
            const int count = esp_http_client_read(client, chunk, sizeof(chunk));
            if (count < 0) { result = ESP_FAIL; break; }
            if (count == 0) {
                if (!esp_http_client_is_complete_data_received(client)) result = ESP_ERR_TIMEOUT;
                break;
            }
            ctx.bytes += static_cast<size_t>(count);
            if (ctx.bytes > ctx.limit) { result = ESP_ERR_INVALID_SIZE; break; }
            if (ctx.parser) {
                if (!ctx.parser->feed(chunk, count)) { result = ESP_FAIL; break; }
                if (ctx.parser->complete() || (ctx.stop && *ctx.stop)) break;
            } else if (ctx.body) ctx.body->append(chunk, count);
        }
        break;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}


bool refresh_sink(void *,gpp::PodcastEpisode&& item) {
    Lock guard;
    to_record(item,io_record);
    return catalog.append(io_record);
}
struct ChapterCapture { const passport_episode_t *snapshot; std::string packed; bool found=false; };
bool chapter_sink(void *context,gpp::PodcastEpisode&& item) {
    auto& capture=*static_cast<ChapterCapture *>(context);
    // The fixed-width public identity is the same canonical snapshot supplied
    // to playback. URL must also match, including when GUID is reused.
    char guid[257]; copy(guid,item.guid);
    if (!strcmp(guid,capture.snapshot->guid) && item.enclosureUrl==capture.snapshot->url) {
        capture.packed=std::move(item.timelinePacked); capture.found=true;
    }
    return true;
}
void import_legacy(int c) {
    if (!storage_open || catalog.count(c) || !catalog.available()) return;
    size_t length=0;
    if (nvs_get_blob(storage,kChannels[c].id,nullptr,&length)!=ESP_OK || !length || length>kMaxCache) return;
    std::string bytes(length,'\0');
    if (nvs_get_blob(storage,kChannels[c].id,&bytes[0],&length)!=ESP_OK) return;
    std::vector<gpp::PodcastEpisode> legacy;
    if (!gpp::CacheCodec::decodeEpisodes(bytes,legacy,5)) return;
    std::string().swap(bytes);
    if (!catalog.begin(c)) return;
    for (auto& item:legacy) {
        clear_extra_metadata(item,c);
        if (item.title.empty() || item.guid.empty() || item.enclosureUrl.empty()) continue;
        to_record(item,io_record);
        if (!catalog.append(io_record)) { catalog.abort(); return; }
    }
    if (!catalog.commit()) catalog.abort();
    // Keep the original NVS blobs: unsupported/new layouts never erase NVS.
}
} // namespace

extern "C" esp_err_t passport_catalog_init(void) {
    if (mutex) return ESP_OK;
    mutex=xSemaphoreCreateMutex();
    if (!mutex) return ESP_ERR_NO_MEM;
    storage_open=nvs_open("gcores",NVS_READWRITE,&storage)==ESP_OK;
    cache_partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"gcores_cache");
    if (cache_partition && !cache_partition->encrypted) {
        const gpp::FlashCatalogIo io{const_cast<esp_partition_t *>(cache_partition),cache_partition->size,flash_read,flash_write,flash_erase};
        catalog.mount(io);
    }
    if (!catalog.available()) ESP_LOGW(kTag,"Flash catalog unavailable; existing NVS and playback history preserved");
    for (int c=0;c<PASSPORT_CHANNEL_COUNT;++c) import_legacy(c);
    size_t length=sizeof(resume);
    have_resume=storage_open && nvs_get_blob(storage,"resume_v1",&resume,&length)==ESP_OK &&
        length==sizeof(resume) && valid_resume(resume);
    return ESP_OK;
}
extern "C" int passport_catalog_channel_count(void) { return PASSPORT_CHANNEL_COUNT; }
extern "C" const char *passport_catalog_channel_name(int c) { return valid_channel(c)?kChannels[c].title:""; }
extern "C" const char *passport_catalog_channel_short_name(int c) { return valid_channel(c)?kChannels[c].shortTitle:""; }
extern "C" const char *passport_catalog_channel_id(int c) { return valid_channel(c)?kChannels[c].id:""; }
extern "C" int passport_catalog_episode_count(int c) { Lock guard; return valid_channel(c)?visible_count(c):0; }
extern "C" bool passport_catalog_get_episode(int c,int e,passport_episode_t *out) {
    Lock guard; return get_episode_locked(c,e,out);
}
extern "C" int passport_catalog_find_episode_by_identity(int c,const char *guid,const char *url) {
    if (!valid_channel(c) || !guid || !*guid || !url || !*url) return -1;
    Lock guard;
    if (playback_channel==c && same_identity(playback,guid,url))
        return playback_index<0 ? int(catalog.count(c)) : playback_index;
    return find_locked(c,guid,url);
}
extern "C" esp_err_t passport_catalog_set_playback(int c,const passport_episode_t *snapshot) {
    if (!valid_channel(c) || !valid_snapshot(snapshot)) return ESP_ERR_INVALID_ARG;
    Lock guard;
    if (playback_channel==c && same_identity(playback,snapshot->guid,snapshot->url)) { playback=*snapshot; return ESP_OK; }
    const int index=find_locked(c,snapshot->guid,snapshot->url);
    if (index>=0) {
        if (!catalog.read(c,index,io_record)) return ESP_FAIL;
    }
    playback=*snapshot; playback_channel=c; playback_index=index;
    if (index>=0) memcpy(playback_chapters,io_record.chaptersUrl,sizeof(playback_chapters));
    else playback_chapters[0]=0;
    // Worker may already have prepared exactly this timeline before pinning.
    if (!timeline_matches(c,snapshot->guid,snapshot->url)) {
        std::string().swap(timeline_packed); timeline_channel=-1;
        timeline_guid[0]=timeline_url[0]=0;
    }
    return ESP_OK;
}
extern "C" esp_err_t passport_catalog_refresh(int c) {
    if (!valid_channel(c) || !mutex) return ESP_ERR_INVALID_ARG;
    if (!network_memory_available()) return ESP_ERR_NO_MEM;
    { Lock guard; if (!catalog.available()) return ESP_ERR_INVALID_STATE; if (!catalog.begin(c)) return ESP_FAIL; }
    esp_err_t error;
    {
        gpp::RssParser parser(PASSPORT_EPISODES_PER_CHANNEL,false,refresh_sink,nullptr,true,
                              gpp::RssParseMode::CatalogMetadata);
        HttpContext ctx; ctx.parser=&parser;
        error=request(kChannels[c].feedUrl,ctx);
        if (error==ESP_OK && !parser.finish().ok) error=ESP_ERR_INVALID_RESPONSE;
    }
    Lock guard;
    if (error!=ESP_OK || !catalog.commit()) { catalog.abort(); return error==ESP_OK?ESP_FAIL:error; }
    page_channel=-1;
    if (playback_channel==c) playback_index=find_locked(c,playback.guid,playback.url);
    // The active timeline/playback identity is independent of this snapshot.
    return ESP_OK;
}
extern "C" esp_err_t passport_catalog_fetch_chapters_for_episode(int c,const passport_episode_t *snapshot) {
    if (!valid_channel(c) || !valid_snapshot(snapshot)) return ESP_ERR_INVALID_ARG;
    std::string chapters_url;
    { Lock guard;
      if (timeline_matches(c,snapshot->guid,snapshot->url) && !timeline_packed.empty()) return ESP_OK;
      if (playback_channel==c && same_identity(playback,snapshot->guid,snapshot->url)) chapters_url=playback_chapters;
      else {
          const int index=find_locked(c,snapshot->guid,snapshot->url);
          if (index>=0) {
              if (!catalog.read(c,index,io_record)) return ESP_FAIL;
              chapters_url=io_record.chaptersUrl;
          }
      }
    }
    // Only one episode timeline is retained. Release an obsolete packed body
    // before fetching another to avoid paying both old cache and TLS/JSON peaks.
    { Lock guard;
      if (!timeline_matches(c,snapshot->guid,snapshot->url)) {
          std::string().swap(timeline_packed); timeline_channel=-1;
          timeline_guid[0]=timeline_url[0]=0;
      }
    }
    if (!network_memory_available()) return ESP_ERR_NO_MEM;
    std::string packed;
    if (!chapters_url.empty()) {
        std::string json; HttpContext ctx; ctx.body=&json; ctx.limit=kMaxChapterBytes;
        esp_err_t error=request(chapters_url.c_str(),ctx);
        if (error!=ESP_OK) return error;
        packed=gpp::TimelineParser::pack(gpp::TimelineParser::parsePodcastChaptersJson(json,snapshot->duration_seconds));
    } else {
        ChapterCapture capture{snapshot,{},false};
        gpp::RssParser parser(PASSPORT_EPISODES_PER_CHANNEL,true,chapter_sink,&capture,true);
        parser.setTimelineIdentity(snapshot->guid,snapshot->url);
        HttpContext ctx; ctx.parser=&parser; ctx.stop=&capture.found;
        esp_err_t error=request(kChannels[c].feedUrl,ctx);
        if (error!=ESP_OK) return error;
        if (!capture.found) return ESP_ERR_NOT_FOUND;
        packed=std::move(capture.packed);
    }
    Lock guard;
    timeline_packed=std::move(packed); timeline_channel=c;
    memcpy(timeline_guid,snapshot->guid,sizeof(timeline_guid)); memcpy(timeline_url,snapshot->url,sizeof(timeline_url));
    return ESP_OK;
}
extern "C" esp_err_t passport_catalog_fetch_chapters(int c,int e) {
    passport_episode_t snapshot;
    if (!passport_catalog_get_episode(c,e,&snapshot)) return ESP_ERR_INVALID_ARG;
    return passport_catalog_fetch_chapters_for_episode(c,&snapshot);
}
extern "C" size_t passport_catalog_chapter_count_by_identity(int c,const char *guid,const char *url) {
    Lock guard; return timeline_matches(c,guid,url)?packed_count():0;
}
extern "C" bool passport_catalog_get_chapter_by_identity(int c,const char *guid,const char *url,size_t index,passport_chapter_t *out) {
    Lock guard; return timeline_matches(c,guid,url) && packed_chapter(index,out);
}
extern "C" bool passport_catalog_current_chapter_by_identity(int c,const char *guid,const char *url,uint32_t position,passport_chapter_t *out,size_t *index) {
    if (!out) return false;
    Lock guard;
    if (!timeline_matches(c,guid,url)) return false;
    gpp::PackedTimelineEntry chapter;
    if (!gpp::TimelineParser::currentPacked(timeline_packed,position,chapter)) return false;
    out->start_seconds=chapter.startSeconds;
    memcpy(out->title,chapter.title.data(),chapter.title.size()); out->title[chapter.title.size()]=0;
    if (index) *index=chapter.index;
    return true;
}
extern "C" size_t passport_catalog_chapter_count(int c,int e) {
    passport_episode_t snapshot;
    return passport_catalog_get_episode(c,e,&snapshot)?passport_catalog_chapter_count_by_identity(c,snapshot.guid,snapshot.url):0;
}
extern "C" bool passport_catalog_get_chapter(int c,int e,size_t index,passport_chapter_t *out) {
    passport_episode_t snapshot;
    return passport_catalog_get_episode(c,e,&snapshot) && passport_catalog_get_chapter_by_identity(c,snapshot.guid,snapshot.url,index,out);
}
extern "C" bool passport_catalog_current_chapter(int c,int e,uint32_t position,passport_chapter_t *out,size_t *index) {
    passport_episode_t snapshot;
    return passport_catalog_get_episode(c,e,&snapshot) && passport_catalog_current_chapter_by_identity(c,snapshot.guid,snapshot.url,position,out,index);
}
extern "C" size_t passport_catalog_get_chapters(int c,int e,passport_chapter_t *out,size_t capacity) {
    passport_episode_t snapshot;
    if (!passport_catalog_get_episode(c,e,&snapshot)) return 0;
    Lock guard;
    if (!timeline_matches(c,snapshot.guid,snapshot.url)) return 0;
    const size_t count=packed_count();
    if (out) for (size_t i=0;i<std::min(count,capacity);++i) packed_chapter(i,out+i);
    return count;
}
extern "C" bool passport_catalog_load_resume(passport_resume_t *out) {
    if (!out) return false;
    Lock guard;
    if (!have_resume) return false;
    *out=resume;
    const int index=find_locked(out->channel,out->guid,out->url);
    out->episode=index>=0?index:UINT8_MAX;
    return true;
}
extern "C" esp_err_t passport_catalog_save_resume(int c,int e,uint32_t seconds,bool completed) {
    passport_episode_t item;
    if (!passport_catalog_get_episode(c,e,&item)) return ESP_ERR_INVALID_ARG;
    passport_resume_t next{};
    next.channel=c; next.episode=e; next.position_seconds=seconds; next.total_seconds=item.duration_seconds; next.completed=completed;
    memcpy(next.guid,item.guid,sizeof(next.guid)); memcpy(next.url,item.url,sizeof(next.url)); memcpy(next.title,item.title,sizeof(next.title));
    return passport_catalog_save_playback(&next);
}
extern "C" esp_err_t passport_catalog_save_playback(const passport_resume_t *snapshot) {
    if (!snapshot || !valid_resume(*snapshot)) return ESP_ERR_INVALID_ARG;
    passport_resume_t next{};
    next.channel = snapshot->channel; next.episode = snapshot->episode;
    next.position_seconds = snapshot->position_seconds;
    next.total_seconds = snapshot->total_seconds; next.completed = snapshot->completed;
    memcpy(next.guid, snapshot->guid, strlen(snapshot->guid));
    memcpy(next.url, snapshot->url, strlen(snapshot->url));
    memcpy(next.title, snapshot->title, strlen(snapshot->title));
    { Lock guard; if (have_resume && memcmp(&next, &resume, sizeof(next)) == 0) return ESP_OK; }
    if (!storage_open) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_blob(storage, "resume_v1", &next, sizeof(next));
    if (err == ESP_OK) err = nvs_commit(storage);
    if (err == ESP_OK) { Lock guard; resume = next; have_resume = true; }
    return err;
}

extern "C" bool passport_catalog_timeline_ready(int c,const char *guid,const char *url) {
    Lock guard; return timeline_matches(c,guid,url) && !timeline_packed.empty();
}
extern "C" void passport_catalog_release_timeline(void) {
    Lock guard; std::string().swap(timeline_packed); timeline_channel=-1;
    timeline_guid[0]=timeline_url[0]=0;
}
