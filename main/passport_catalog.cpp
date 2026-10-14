/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from sonicpic/gcores-pocket-player; see licenses/gcores-GPL-3.0.txt.
 */
#include "passport_catalog.h"
#include "gcores/rss/RssParser.h"
#include "gcores/rss/TextUtils.h"
#include "gcores/storage/CacheCodec.h"
#include "gcores/storage/ResumeValidation.h"
#include "gcores/timeline/TimelineParser.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
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
constexpr size_t kMaxCache = 16384;
constexpr size_t kMaxFeedBytes = 1024 * 1024;
constexpr size_t kMaxChapterBytes = 32768;
constexpr const char *kTag = "catalog";
const gpp::PodcastChannel kChannels[] = {
    {"games", "机核游戏频道", "游戏", "GADIO", "https://alioss.gcores.com/feeds/apple_podcast_v1.xml"},
    {"life", "机核生活频道", "生活", "Gadio Life", "https://alioss.gcores.com/feeds/gadio_life.xml"},
    {"pro", "机核兴趣频道", "兴趣", "Gadio Pro", "https://alioss.gcores.com/feeds/gadio_pro.xml"},
    {"heshi", "核市奇谭", "核市", "核市奇谭", "https://alioss.gcores.com/feeds/heshi.xml"},
    {"yinping", "银屏系漫游指南", "银屏", "银屏系漫游指南", "https://alioss.gcores.com/feeds/yin_ping.xml"},
};
std::array<std::vector<gpp::PodcastEpisode>, PASSPORT_CHANNEL_COUNT> episodes;
SemaphoreHandle_t mutex;
nvs_handle_t storage;
bool storage_open;
passport_resume_t resume{};
bool have_resume;
bool valid_resume(const passport_resume_t &value) {
    const auto *raw = reinterpret_cast<const unsigned char *>(&value);
    return gpp::validResumeIdentity(value.channel, value.episode,
        raw[offsetof(passport_resume_t, completed)], value.guid, sizeof(value.guid),
        value.url, sizeof(value.url), value.title, sizeof(value.title));
}

struct Lock {
    Lock() { if (mutex) xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { if (mutex) xSemaphoreGive(mutex); }
};
bool valid_channel(int c) { return c >= 0 && c < PASSPORT_CHANNEL_COUNT; }
bool valid_episode(int c, int e) {
    return valid_channel(c) && e >= 0 && static_cast<size_t>(e) < episodes[c].size();
}
template<size_t N> void copy(char (&out)[N], const std::string &in) {
    size_t n = std::min(in.size(), N - 1);
    while (n && n < in.size() && (static_cast<unsigned char>(in[n]) & 0xc0) == 0x80) --n;
    memcpy(out, in.data(), n); out[n] = 0;
}
void clear_extra_metadata(gpp::PodcastEpisode &e, int c) {
    e.channelId = kChannels[c].id;
    /* clear() leaves the discarded metadata's heap capacity resident across
     * all 25 episodes. Swap empty storage in before retaining the catalogue. */
    /* UI uses title, duration and timeline; discard unused episode summaries.
     * Keep the cache field empty without changing the persisted format. */
    std::string().swap(e.description);
    std::string().swap(e.contentEncoded);
    std::vector<gpp::TimelineEntry>().swap(e.timeline);
    std::string().swap(e.link); std::string().swap(e.author);
    std::string().swap(e.coverUrl); std::string().swap(e.pubDate);
    std::string().swap(e.enclosureType); std::string().swap(e.chaptersType);
}

struct HttpContext {
    gpp::RssParser *parser = nullptr;
    std::string *body = nullptr;
    size_t bytes = 0;
    size_t limit = kMaxFeedBytes;
    std::string redirect;
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
                ctx.parser->feed(chunk, count);
                if (ctx.parser->complete()) break;
            } else if (ctx.body) ctx.body->append(chunk, count);
        }
        break;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

/* Cache is lean metadata only; only one expanded timeline is retained in RAM.
 * This keeps 25 episode records affordable on a no-PSRAM ESP32-C3. */
esp_err_t save_cache(int channel, const std::vector<gpp::PodcastEpisode> &items) {
    if (!storage_open) return ESP_ERR_INVALID_STATE;
    /* The sole caller already strips extra metadata and timelines. Encode
     * those same lean records directly, avoiding another episode-sized copy. */
    if (gpp::CacheCodec::encodedSize(items) > kMaxCache) return ESP_ERR_INVALID_SIZE;
    std::string bytes = gpp::CacheCodec::encodeEpisodes(items);
    size_t old_length = 0;
    const char *key = kChannels[channel].id;
    if (nvs_get_blob(storage, key, nullptr, &old_length) == ESP_OK && old_length == bytes.size()) {
        std::string old(old_length, '\0');
        if (nvs_get_blob(storage, key, &old[0], &old_length) == ESP_OK && old == bytes) return ESP_OK;
    }
    /* Write a staged copy first. Commit failures do not clear the last cache.
     * NVS atomically replaces blobs; no erase or whole-partition recovery. */
    esp_err_t err = nvs_set_blob(storage, "pending", bytes.data(), bytes.size());
    if (err == ESP_OK) err = nvs_commit(storage);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(storage, key, bytes.data(), bytes.size());
    if (err == ESP_OK) err = nvs_commit(storage);
    if (err == ESP_OK) {
        nvs_erase_key(storage, "pending");
        nvs_commit(storage);
    }
    return err;
}
void evict_timelines() {
    for (auto &channel : episodes) for (auto &e : channel) {
        std::string().swap(e.timelinePacked);
        std::vector<gpp::TimelineEntry>().swap(e.timeline);
    }
}
} // namespace

extern "C" esp_err_t passport_catalog_init(void) {
    if (mutex) return ESP_OK;
    mutex = xSemaphoreCreateMutex();
    if (!mutex) return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_open("gcores", NVS_READWRITE, &storage);
    storage_open = err == ESP_OK;
    if (!storage_open) { ESP_LOGW(kTag, "NVS unavailable: %s", esp_err_to_name(err)); return ESP_OK; }
    for (int c = 0; c < PASSPORT_CHANNEL_COUNT; ++c) {
        size_t length = 0;
        if (nvs_get_blob(storage, kChannels[c].id, nullptr, &length) != ESP_OK || !length || length > kMaxCache) continue;
        std::string bytes(length, '\0');
        if (nvs_get_blob(storage, kChannels[c].id, &bytes[0], &length) != ESP_OK) continue;
        if (!gpp::CacheCodec::decodeEpisodes(bytes, episodes[c], PASSPORT_EPISODES_PER_CHANNEL)) continue;
        for (auto &e : episodes[c]) clear_extra_metadata(e, c);
    }
    size_t length = sizeof(resume);
    have_resume = nvs_get_blob(storage, "resume_v1", &resume, &length) == ESP_OK &&
                  length == sizeof(resume) && valid_resume(resume);
    return ESP_OK;
}
extern "C" int passport_catalog_channel_count(void) { return PASSPORT_CHANNEL_COUNT; }
extern "C" const char *passport_catalog_channel_name(int c) { return valid_channel(c) ? kChannels[c].title : ""; }
extern "C" const char *passport_catalog_channel_short_name(int c) { return valid_channel(c) ? kChannels[c].shortTitle : ""; }
extern "C" const char *passport_catalog_channel_id(int c) { return valid_channel(c) ? kChannels[c].id : ""; }
extern "C" int passport_catalog_episode_count(int c) { Lock guard; return valid_channel(c) ? episodes[c].size() : 0; }
extern "C" bool passport_catalog_get_episode(int c, int e, passport_episode_t *out) {
    if (!out) return false;
    Lock guard;
    if (!valid_episode(c, e)) return false;
    const auto &item = episodes[c][e];
    memset(out, 0, sizeof(*out));
    copy(out->title, item.title); copy(out->guid, item.guid); copy(out->url, item.enclosureUrl);
    out->duration_seconds = item.durationSeconds; out->enclosure_length = item.enclosureLength;
    return true;
}
extern "C" esp_err_t passport_catalog_refresh(int c) {
    if (!valid_channel(c) || !mutex) return ESP_ERR_INVALID_ARG;
    gpp::RssParseResult result;
    {
        gpp::RssParser parser(PASSPORT_EPISODES_PER_CHANNEL, false);
        HttpContext ctx; ctx.parser = &parser;
        esp_err_t err = request(kChannels[c].feedUrl, ctx);
        if (err != ESP_OK) return err;
        result = parser.finish();
    } // Release streaming XML/prefix buffers before allocating the cache blob.
    if (!result.ok) return ESP_ERR_INVALID_RESPONSE;
    for (auto &e : result.episodes) {
        clear_extra_metadata(e, c);
        std::string().swap(e.timelinePacked);
    }
    result.episodes.erase(std::remove_if(result.episodes.begin(), result.episodes.end(), [](const auto &e) {
        return e.enclosureUrl.empty() || e.title.empty();
    }), result.episodes.end());
    if (result.episodes.empty()) return ESP_ERR_INVALID_RESPONSE;
    esp_err_t err = save_cache(c, result.episodes);
    if (err != ESP_OK) ESP_LOGW(kTag, "Cache not persisted (%s); existing NVS preserved", esp_err_to_name(err));
    { Lock guard; episodes[c] = std::move(result.episodes); }
    return ESP_OK;
}
extern "C" int passport_catalog_find_episode_by_identity(int c, const char *guid, const char *url) {
    if (!guid || !*guid || !url || !*url) return -1;
    Lock guard;
    if (!valid_channel(c)) return -1;
    for (size_t i = 0; i < std::min(episodes[c].size(), size_t(PASSPORT_EPISODES_PER_CHANNEL)); ++i)
        if (episodes[c][i].guid == guid && episodes[c][i].enclosureUrl == url) return static_cast<int>(i);
    return -1;
}
extern "C" size_t passport_catalog_get_chapters(int c, int e, passport_chapter_t *out, size_t capacity) {
    Lock guard;
    if (!valid_episode(c, e)) return 0;
    std::vector<gpp::TimelineEntry> chapter;
    if (!gpp::TimelineParser::unpack(episodes[c][e].timelinePacked, chapter)) return 0;
    const size_t count = chapter.size();
    if (out) for (size_t i = 0; i < std::min(count, capacity); ++i) {
        out[i].start_seconds = chapter[i].startSeconds; copy(out[i].title, chapter[i].title);
    }
    return count;
}
extern "C" size_t passport_catalog_chapter_count(int c, int e) {
    Lock guard;
    if (!valid_episode(c, e)) return 0;
    const auto &packed = episodes[c][e].timelinePacked;
    if (packed.size() < 2) return 0;
    const size_t count = static_cast<unsigned char>(packed[0]) |
                         (static_cast<size_t>(static_cast<unsigned char>(packed[1])) << 8);
    return count <= PASSPORT_MAX_CHAPTERS ? count : 0;
}
extern "C" bool passport_catalog_get_chapter(int c, int e, size_t index, passport_chapter_t *out) {
    if (!out) return false;
    Lock guard;
    if (!valid_episode(c, e)) return false;
    const auto &packed = episodes[c][e].timelinePacked;
    if (packed.size() < 2) return false;
    const size_t count = static_cast<unsigned char>(packed[0]) |
                         (static_cast<size_t>(static_cast<unsigned char>(packed[1])) << 8);
    if (count > PASSPORT_MAX_CHAPTERS || index >= count) return false;
    size_t pos = 2;
    for (size_t i = 0; i <= index; ++i) {
        if (pos + 6 > packed.size()) return false;
        uint32_t seconds = 0;
        for (unsigned b = 0; b < 4; ++b) seconds |= static_cast<uint32_t>(static_cast<unsigned char>(packed[pos++])) << (8 * b);
        const size_t length = static_cast<unsigned char>(packed[pos]) |
                              (static_cast<size_t>(static_cast<unsigned char>(packed[pos+1])) << 8);
        pos += 2;
        if (length > 180 || pos + length > packed.size()) return false;
        if (i == index) {
            out->start_seconds = seconds;
            memcpy(out->title, packed.data() + pos, length); out->title[length] = 0;
            return true;
        }
        pos += length;
    }
    return false;
}
extern "C" bool passport_catalog_current_chapter(int c, int e, uint32_t position,
                                                  passport_chapter_t *out, size_t *index) {
    if (!out) return false;
    Lock guard;
    if (!valid_episode(c, e)) return false;
    gpp::PackedTimelineEntry chapter;
    if (!gpp::TimelineParser::currentPacked(episodes[c][e].timelinePacked, position, chapter)) return false;
    out->start_seconds = chapter.startSeconds;
    memcpy(out->title, chapter.title.data(), chapter.title.size());
    out->title[chapter.title.size()] = 0;
    if (index) *index = chapter.index;
    return true;
}
extern "C" esp_err_t passport_catalog_fetch_chapters(int c, int e) {
    std::string guid, chapters_url; uint32_t duration = 0;
    { Lock guard;
      if (!valid_episode(c, e)) return ESP_ERR_INVALID_ARG;
      if (!episodes[c][e].timelinePacked.empty()) return ESP_OK;
      guid = episodes[c][e].guid; chapters_url = episodes[c][e].chaptersUrl;
      duration = episodes[c][e].durationSeconds;
      evict_timelines();
    }
    std::string packed;
    if (!chapters_url.empty()) {
        std::string json;
        HttpContext ctx; ctx.body = &json; ctx.limit = kMaxChapterBytes;
        esp_err_t err = request(chapters_url.c_str(), ctx);
        if (err != ESP_OK) return err;
        packed = gpp::TimelineParser::pack(gpp::TimelineParser::parsePodcastChaptersJson(json, duration));
    } else {
        gpp::RssParser parser(PASSPORT_EPISODES_PER_CHANNEL);
        parser.setTimelineGuid(guid);
        HttpContext ctx; ctx.parser = &parser;
        esp_err_t err = request(kChannels[c].feedUrl, ctx);
        if (err != ESP_OK) return err;
        auto result = parser.finish();
        for (auto &item : result.episodes) if (item.guid == guid) { packed = std::move(item.timelinePacked); break; }
    }
    Lock guard;
    if (!valid_episode(c, e) || episodes[c][e].guid != guid) return ESP_ERR_INVALID_STATE;
    episodes[c][e].timelinePacked = std::move(packed);
    return ESP_OK;
}
extern "C" bool passport_catalog_load_resume(passport_resume_t *out) {
    if (!out) return false;
    Lock guard;
    if (!have_resume) return false;
    *out = resume;
    /* Index changes whenever the RSS advances; resume matches by GUID. */
    out->episode = UINT8_MAX;
    if (valid_channel(out->channel)) for (size_t i = 0; i < episodes[out->channel].size(); ++i) {
        if (episodes[out->channel][i].guid == out->guid) { out->episode = i; break; }
    }
    return true;
}
extern "C" esp_err_t passport_catalog_save_resume(int c, int e, uint32_t seconds, bool completed) {
    passport_resume_t next{};
    { Lock guard;
      if (!valid_episode(c, e)) return ESP_ERR_INVALID_ARG;
      const auto &item = episodes[c][e];
      next.channel = c; next.episode = e; next.position_seconds = seconds;
      next.total_seconds = item.durationSeconds; next.completed = completed;
      copy(next.guid, item.guid); copy(next.url, item.enclosureUrl); copy(next.title, item.title);
    }
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
