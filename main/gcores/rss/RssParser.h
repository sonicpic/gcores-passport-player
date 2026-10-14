/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "Models.h"

namespace gpp {
struct RssParseResult {
  bool ok = false;
  bool truncatedItem = false;
  std::string channelTitle;
  std::string error;
  std::vector<PodcastEpisode> episodes;
};

class RssParser {
 public:
  explicit RssParser(size_t limit = 10, bool parseTimeline = true);
  void setTimelineGuid(const std::string& guid) { timelineGuid_ = guid; }
  bool feed(const char* data, size_t length);
  bool complete() const { return episodes_.size() >= limit_; }
  size_t episodeCount() const { return episodes_.size(); }
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
};
}
