/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../rss/Models.h"

#if !defined(GPP_NATIVE_TEST) && !defined(ESP_PLATFORM)
class Print;
class Stream;
#endif

namespace gpp {
class CacheCodec {
 public:
  static std::string encodeEpisodes(const std::vector<PodcastEpisode>& episodes);
  static bool decodeEpisodes(const std::string& bytes, std::vector<PodcastEpisode>& episodes,
                             size_t maxEpisodes = 20);
#if !defined(GPP_NATIVE_TEST) && !defined(ESP_PLATFORM)
  // Device-side overloads avoid creating a second cache-sized std::string.
  // byteLength is mandatory so a truncated/corrupt stream cannot grow memory
  // without a bound.
  static bool encodeEpisodes(Print& output, const std::vector<PodcastEpisode>& episodes,
                             size_t* bytesWritten = nullptr);
  static bool decodeEpisodes(Stream& input, size_t byteLength,
                             std::vector<PodcastEpisode>& episodes,
                             size_t maxEpisodes = 20);
#endif
  static size_t encodedSize(const std::vector<PodcastEpisode>& episodes);
  static bool supportedHeader(const char* data, size_t length);
};
}
