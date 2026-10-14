/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "../rss/Models.h"

namespace gpp {
/* A borrowed view into packed storage, valid only while that string remains
 * alive and unchanged. Selection does not allocate or expand the timeline. */
struct PackedTimelineEntry {
  uint32_t startSeconds = 0;
  std::string_view title;
  size_t index = 0;
};
class TimelineParser {
 public:
  static constexpr size_t kMaxPackedBytes = 6 * 1024;
  static std::vector<TimelineEntry> parseText(const std::string& htmlOrText,
                                               uint32_t durationSeconds = 0,
                                               size_t maxEntries = 80);
  static std::vector<TimelineEntry> parsePodcastChaptersJson(const std::string& json,
                                                              uint32_t durationSeconds = 0,
                                                              size_t maxEntries = 80);
  static bool parseTime(const std::string& value, uint32_t& seconds);
  static int currentIndex(const std::vector<TimelineEntry>& entries, uint32_t positionSeconds);
  /* Returns the last entry at/before position. Validates all bounded records
   * before publishing; no current entry/malformed input leaves result intact. */
  static bool currentPacked(const std::string& packed, uint32_t positionSeconds,
                            PackedTimelineEntry& result);
  /* Preserve a whole-record prefix: at most 80 chapters and 6 KiB, including
   * headers. Titles are UTF-8 safely limited to 180 bytes; the tail is omitted. */
  static std::string pack(const std::vector<TimelineEntry>& entries);
  static bool unpack(const std::string& packed, std::vector<TimelineEntry>& entries,
                     size_t maxEntries = 80);
};
}
