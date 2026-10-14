/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "Models.h"

namespace gpp {
enum class RssParseMode { Full, CatalogMetadata };
struct RssParseResult {
  bool ok = false;
  bool truncatedItem = false;
  std::string channelTitle;
  std::string error;
  std::vector<PodcastEpisode> episodes;
  size_t emittedCount = 0;
  bool sinkFailed = false;
};

class RssParser {
 public:
  using EpisodeSink = bool (*)(void *context, PodcastEpisode&& episode);
  explicit RssParser(size_t limit = 10, bool parseTimeline = true,
                     EpisodeSink sink = nullptr, void *context = nullptr,
                     bool requirePlayable = false,
                     RssParseMode mode = RssParseMode::Full);
  void setTimelineGuid(const std::string& guid) { timelineGuid_ = guid; timelineUrl_.clear(); }
  void setTimelineIdentity(const std::string& guid, const std::string& url) {
    timelineGuid_ = guid; timelineUrl_ = url;
  }
  bool feed(const char* data, size_t length);
  bool complete() const { return emitted_ >= limit_ || sinkFailed_; }
  size_t episodeCount() const { return emitted_; }
  RssParseResult finish();
  static RssParseResult parseDocument(const std::string& xml, size_t limit = 10, size_t chunkSize = 257);
  static uint32_t parseDuration(const std::string& value);

 private:
  void consume();
  PodcastEpisode parseItem(std::string_view item);
  size_t limit_;
  std::string buffer_;
  std::string channelPrefix_;
  std::vector<PodcastEpisode> episodes_;
  bool skippingOversize_ = false;
  bool truncatedItem_ = false;
  bool parseTimeline_ = true;
  std::string timelineGuid_;
  std::string timelineUrl_;
  EpisodeSink sink_ = nullptr;
  void *sinkContext_ = nullptr;
  size_t emitted_ = 0;
  bool sinkFailed_ = false, requirePlayable_ = false;
  bool metadataOnly_ = false;
};
}
