/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gpp {

struct PodcastChannel {
  // Built-in sources live in flash and do not consume one heap allocation per
  // field. Future SD/user sources can be represented by a separate owned
  // catalogue without changing the playback model.
  const char* id;
  const char* title;
  const char* shortTitle;
  const char* playerTitle;
  const char* feedUrl;
};

struct TimelineEntry {
  uint32_t startSeconds = 0;
  std::string title;
  std::string raw;
};

struct PodcastEpisode {
  std::string channelId;
  std::string title;
  std::string guid;
  std::string pubDate;
  std::string enclosureUrl;
  std::string enclosureType;
  uint64_t enclosureLength = 0;
  uint32_t durationSeconds = 0;
  std::string description;
  std::string contentEncoded;
  std::string link;
  std::string author;
  std::string coverUrl;
  std::string chaptersUrl;
  std::string chaptersType;
  // Populated from the SD download manifest.  An old downloaded episode is
  // merged into the five fresh RSS items and remains visible indefinitely.
  std::string localAudioPath;
  bool downloaded = false;
  // Compact timeline representation used by episode lists and cache. The
  // vector below is expanded only for the currently selected/playing item.
  std::string timelinePacked;
  std::vector<TimelineEntry> timeline;
  bool timelineParsed = false;
};

struct PlaybackState {
  std::string guid;
  std::string channelId;
  std::string enclosureUrl;
  std::string title;
  uint32_t positionSeconds = 0;
  uint32_t totalSeconds = 0;
  uint64_t lastPlayedUnix = 0;
  bool completed = false;
};

struct UserSettings {
  uint8_t episodeCount = 5;
  uint8_t volume = 35;
  uint8_t brightness = 160;
  uint32_t screenTimeoutSeconds = 30;
  uint16_t skipBackSeconds = 15;
  uint16_t skipForwardSeconds = 30;
  uint32_t sleepSeconds = 0;
  bool sleepAfterEpisode = false;
};

enum class NetworkStatus { Off, Disconnected, Connecting, Connected, Error };
struct NetworkState {
  NetworkStatus status = NetworkStatus::Off;
  int8_t rssi = 0;
  std::string message;
};

}
