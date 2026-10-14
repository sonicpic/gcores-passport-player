/* SPDX-License-Identifier: GPL-3.0-only */
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include "flash_fixture.h"
#include "../../main/passport_catalog.cpp"
static FlashFixture flash;
static std::map<std::string,std::string> nvs;
static std::string http_body;
static size_t http_fail_after=SIZE_MAX;
static int http_status=200;
static bool partition_missing;
static uint32_t free_heap=200*1024;
static size_t largest_heap=100*1024;
uint32_t esp_get_free_heap_size() { return free_heap; }
size_t heap_caps_get_largest_free_block(unsigned) { return largest_heap; }
struct StubHttpClient { size_t cursor=0; };
const esp_partition_t *esp_partition_find_first(int,int,const char *) {
    static esp_partition_t partition{gpp::FlashCatalog::kRequiredBytes,false};
    return partition_missing?nullptr:&partition;
}
esp_err_t esp_partition_read(const esp_partition_t *,size_t o,void *p,size_t n) { return FlashFixture::read(&flash,o,p,n)?ESP_OK:ESP_FAIL; }
esp_err_t esp_partition_write(const esp_partition_t *,size_t o,const void *p,size_t n) { return FlashFixture::write(&flash,o,p,n)?ESP_OK:ESP_FAIL; }
esp_err_t esp_partition_erase_range(const esp_partition_t *,size_t o,size_t n) { return FlashFixture::erase(&flash,o,n)?ESP_OK:ESP_FAIL; }
esp_err_t nvs_open(const char *,int,nvs_handle_t *handle) { *handle=1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t,const char *key,void *out,size_t *length) {
    auto found=nvs.find(key); if (found==nvs.end()) return ESP_ERR_NOT_FOUND;
    if (out && *length<found->second.size()) return ESP_ERR_INVALID_SIZE;
    *length=found->second.size(); if (out) std::memcpy(out,found->second.data(),*length); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t,const char *key,const void *bytes,size_t length) { nvs[key]=std::string(static_cast<const char *>(bytes),length); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *) { return new StubHttpClient; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *) { return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t client,int) { client->cursor=0; return ESP_OK; }
int esp_http_client_fetch_headers(esp_http_client_handle_t) { return int(http_body.size()); }
int esp_http_client_get_status_code(esp_http_client_handle_t) { return http_status; }
int esp_http_client_read(esp_http_client_handle_t client,char *out,int bytes) {
    if (client->cursor>=http_fail_after) return -1;
    const size_t n=std::min(size_t(bytes),http_body.size()-client->cursor);
    std::memcpy(out,http_body.data()+client->cursor,n); client->cursor+=n; return int(n);
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) { return client->cursor==http_body.size(); }
esp_err_t esp_http_client_close(esp_http_client_handle_t) { return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { delete client; return ESP_OK; }
esp_err_t esp_http_client_set_url(esp_http_client_handle_t,const char *) { return ESP_OK; }
static std::string feed(size_t count,const char *prefix="E") {
    std::string xml="<rss><channel><title>Gcores</title>";
    for (size_t i=0;i<count;++i) {
        const auto id=std::string(prefix)+std::to_string(i);
        xml+="<item><title>节目"+id+"</title><guid>"+id+"</guid>"
          "<description><![CDATA[00:00 开场<br>00:05 第二章]]></description>"
          "<enclosure url='https://gcores.com/"+id+".mp3' length='123456'/>"
          "<itunes:duration>00:10</itunes:duration></item>";
    }
    return xml+"</channel></rss>";
}
static std::string identity_item(const std::string& url,const char *chapter,
                                 const std::string& guid="",const std::string& link="") {
    return "<item><title>Identity</title><guid>"+guid+"</guid><link>"+link+"</link>"
        "<description><![CDATA[00:00 "+chapter+"]]></description>"
        "<enclosure url='"+url+"'/><itunes:duration>00:10</itunes:duration></item>";
}
static void check_identity_chapters(int channel,const std::string& first,const std::string& second) {
    http_body="<rss><channel>"+first+second+"</channel></rss>";
    assert(passport_catalog_refresh(channel)==ESP_OK);
    passport_episode_t wrong{},target{};
    assert(passport_catalog_get_episode(channel,0,&wrong));
    assert(passport_catalog_get_episode(channel,1,&target));
    assert(!std::strcmp(wrong.guid,target.guid) && std::strcmp(wrong.url,target.url));
    assert(passport_catalog_fetch_chapters_for_episode(channel,&target)==ESP_OK);
    passport_chapter_t chapter{};
    assert(passport_catalog_get_chapter_by_identity(channel,target.guid,target.url,0,&chapter));
    assert(!std::strcmp(chapter.title,"Target"));
    assert(!passport_catalog_get_chapter_by_identity(channel,wrong.guid,wrong.url,0,&chapter));
}
int main(int argc,char **) {
    if (argc>1) {
        partition_missing=true;
        assert(passport_catalog_init()==ESP_OK && passport_catalog_episode_count(0)==0);
        assert(passport_catalog_refresh(0)==ESP_ERR_INVALID_STATE);
        passport_episode_t saved{}; std::strcpy(saved.title,"Saved"); std::strcpy(saved.guid,"saved");
        std::strcpy(saved.url,"https://gcores.com/saved.mp3");
        assert(passport_catalog_set_playback(0,&saved)==ESP_OK && passport_catalog_episode_count(0)==1);
        assert(passport_catalog_get_episode(0,0,&saved));
        std::puts("PASS production catalog missing partition graceful playback snapshot"); return 0;
    }
    std::vector<gpp::PodcastEpisode> legacy;
    for (size_t i=0;i<5;++i) { gpp::PodcastEpisode e; e.title="Old"; e.guid="old"+std::to_string(i); e.enclosureUrl="https://gcores.com/old"+std::to_string(i)+".mp3"; legacy.push_back(e); }
    const auto legacy_blob=gpp::CacheCodec::encodeEpisodes(legacy);
    nvs["games"]=legacy_blob; nvs["life"]="unsupported format";
    assert(passport_catalog_init()==ESP_OK && passport_catalog_episode_count(0)==5);
    assert(passport_catalog_episode_count(1)==0 && nvs["games"]==legacy_blob);
    passport_episode_t snapshot{};
    assert(passport_catalog_get_episode(0,4,&snapshot) && !std::strcmp(snapshot.guid,"old4"));
    http_body=feed(101);
    assert(passport_catalog_refresh(0)==ESP_OK && passport_catalog_episode_count(0)==100);
    for (int i : {0,3,4,7,8,99}) assert(passport_catalog_get_episode(0,i,&snapshot));
    assert(!passport_catalog_get_episode(0,100,&snapshot));
    assert(passport_catalog_get_episode(0,90,&snapshot));
    const auto before_low_heap_reads=flash.reads, before_low_heap_writes=flash.writes;
    free_heap=79*1024;
    assert(passport_catalog_refresh(0)==ESP_ERR_NO_MEM);
    assert(passport_catalog_fetch_chapters_for_episode(0,&snapshot)==ESP_ERR_NO_MEM);
    free_heap=200*1024; largest_heap=31*1024;
    assert(passport_catalog_refresh(0)==ESP_ERR_NO_MEM);
    assert(passport_catalog_fetch_chapters_for_episode(0,&snapshot)==ESP_ERR_NO_MEM);
    assert(flash.writes==before_low_heap_writes && flash.reads>=before_low_heap_reads);
    largest_heap=100*1024;
    assert(passport_catalog_fetch_chapters(0,90)==ESP_OK);
    assert(passport_catalog_set_playback(0,&snapshot)==ESP_OK);
    passport_chapter_t chapter{};
    assert(passport_catalog_current_chapter_by_identity(0,snapshot.guid,snapshot.url,5,&chapter,nullptr));
    assert(chapter.start_seconds==5 && !std::strcmp(chapter.title,"第二章"));
    const size_t before_reads=flash.reads;
    assert(passport_catalog_chapter_count_by_identity(0,snapshot.guid,snapshot.url)==2);
    assert(passport_catalog_get_chapter_by_identity(0,snapshot.guid,snapshot.url,0,&chapter));
    assert(flash.reads==before_reads);
    // Failed transport, empty feed, limit and Flash failures retain committed metadata.
    http_body=feed(100,"Changed"); http_fail_after=2048;
    assert(passport_catalog_refresh(0)!=ESP_OK); http_fail_after=SIZE_MAX;
    passport_episode_t old{}; assert(passport_catalog_get_episode(0,90,&old) && !std::strcmp(old.guid,"E90"));
    http_body="<rss><channel></channel></rss>"; assert(passport_catalog_refresh(0)!=ESP_OK);
    http_body="<rss><channel><item><title>Huge</title>"+std::string(2*1024*1024,'x')+"</item></channel></rss>";
    assert(passport_catalog_refresh(0)==ESP_ERR_INVALID_SIZE);
    http_body=feed(100,"Changed"); flash.fail_write=flash.writes+1;
    assert(passport_catalog_refresh(0)!=ESP_OK); flash.fail_write=SIZE_MAX;
    assert(passport_catalog_episode_count(0)==100);
    assert(passport_catalog_refresh(0)==ESP_OK && passport_catalog_episode_count(0)==101);
    assert(passport_catalog_get_episode(0,100,&old) && !std::strcmp(old.guid,snapshot.guid));
    assert(passport_catalog_find_episode_by_identity(0,snapshot.guid,snapshot.url)==100);
    assert(passport_catalog_current_chapter_by_identity(0,snapshot.guid,snapshot.url,5,&chapter,nullptr));
    assert(!passport_catalog_current_chapter_by_identity(0,snapshot.guid,"wrong.mp3",5,&chapter,nullptr));
    assert(passport_catalog_save_resume(0,100,5,false)==ESP_OK);
    passport_resume_t saved{}; assert(passport_catalog_load_resume(&saved));
    assert(saved.episode==UINT8_MAX && !std::strcmp(saved.guid,snapshot.guid) && saved.position_seconds==5);
    assert(nvs["games"]==legacy_blob);
    // Long fallback URLs share a bounded GUID but must retain distinct timelines.
    const auto prefix=std::string("https://gcores.com/")+std::string(300,'x');
    check_identity_chapters(1,identity_item(prefix+"a.mp3","Wrong"),
                              identity_item(prefix+"b.mp3","Target"));
    // Explicit reused GUIDs also require the full URL to match.
    check_identity_chapters(2,identity_item("https://gcores.com/a.mp3","Wrong","same"),
                              identity_item("https://gcores.com/b.mp3","Target","same"));
    // UTF-8 cut inside a fallback link must use the same canonical boundary.
    const auto link=std::string(255,'x')+"中文";
    check_identity_chapters(3,identity_item("https://gcores.com/c.mp3","Wrong","",link),
                              identity_item("https://gcores.com/d.mp3","Target","",link));
    std::puts("PASS actual catalog Flash pages, legacy import, HTTP rollback, limits, stable pin/timeline/resume");
}
