/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#include "CacheCodec.h"

#include <cstring>
#include <limits>

#if !defined(GPP_NATIVE_TEST) && !defined(ESP_PLATFORM)
#include <Arduino.h>
#endif

namespace gpp {
namespace {
// V3 stores lean summaries and packed timelines. Deliberately rejecting older
// HTML-heavy caches prevents them from recreating the heap spike.
constexpr char kMagic[] = "GPPC3";
constexpr size_t kMagicSize = sizeof(kMagic) - 1;
constexpr size_t kMaxField = 16384;

class StringWriter {
 public:
  explicit StringWriter(std::string& output) : output_(output) {}
  bool write(const void* data, size_t length) {
    if (!data && length) return false;
    output_.append(static_cast<const char*>(data), length);
    written_ += length;
    return true;
  }
  size_t written() const { return written_; }

 private:
  std::string& output_;
  size_t written_ = 0;
};

class StringReader {
 public:
  explicit StringReader(const std::string& input) : input_(input) {}
  bool read(void* output, size_t length) {
    if (length > remaining()) return false;
    if (length) std::memcpy(output, input_.data() + position_, length);
    position_ += length;
    return true;
  }
  size_t remaining() const { return input_.size() - position_; }

 private:
  const std::string& input_;
  size_t position_ = 0;
};

#if !defined(GPP_NATIVE_TEST) && !defined(ESP_PLATFORM)
class PrintWriter {
 public:
  explicit PrintWriter(Print& output) : output_(output) {}
  bool write(const void* data, size_t length) {
    if (!data && length) return false;
    const size_t count = output_.write(static_cast<const uint8_t*>(data), length);
    written_ += count;
    return count == length;
  }
  size_t written() const { return written_; }

 private:
  Print& output_;
  size_t written_ = 0;
};

class StreamReader {
 public:
  StreamReader(Stream& input, size_t length) : input_(input), remaining_(length) {}
  bool read(void* output, size_t length) {
    if (length > remaining_) return false;
    if (length && input_.readBytes(static_cast<char*>(output), length) != length) return false;
    remaining_ -= length;
    return true;
  }
  size_t remaining() const { return remaining_; }

 private:
  Stream& input_;
  size_t remaining_;
};
#endif

template <typename Writer>
bool put32(Writer& output, uint32_t value) {
  uint8_t bytes[4];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (value >> (i * 8)) & 0xff;
  return output.write(bytes, sizeof(bytes));
}

template <typename Writer>
bool put64(Writer& output, uint64_t value) {
  uint8_t bytes[8];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (value >> (i * 8)) & 0xff;
  return output.write(bytes, sizeof(bytes));
}

template <typename Writer>
bool putString(Writer& output, const std::string& value) {
  if (value.size() > std::numeric_limits<uint32_t>::max()) return false;
  return put32(output, static_cast<uint32_t>(value.size())) &&
         output.write(value.data(), value.size());
}

template <typename Reader>
bool get32(Reader& input, uint32_t& value) {
  uint8_t bytes[4];
  if (!input.read(bytes, sizeof(bytes))) return false;
  value = 0;
  for (unsigned i = 0; i < sizeof(bytes); ++i) value |= static_cast<uint32_t>(bytes[i]) << (i * 8);
  return true;
}

template <typename Reader>
bool get64(Reader& input, uint64_t& value) {
  uint8_t bytes[8];
  if (!input.read(bytes, sizeof(bytes))) return false;
  value = 0;
  for (unsigned i = 0; i < sizeof(bytes); ++i) value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
  return true;
}

template <typename Reader>
bool getString(Reader& input, std::string& value, size_t maxLength = kMaxField) {
  uint32_t length = 0;
  if (!get32(input, length) || length > maxLength || length > input.remaining()) return false;
  value.resize(length);
  return !length || input.read(&value[0], length);
}

template <typename Writer>
bool putEpisode(Writer& output, const PodcastEpisode& episode) {
  return putString(output, episode.channelId) && putString(output, episode.title) &&
         putString(output, episode.guid) && putString(output, episode.pubDate) &&
         putString(output, episode.enclosureUrl) && putString(output, episode.enclosureType) &&
         put64(output, episode.enclosureLength) && put32(output, episode.durationSeconds) &&
         putString(output, episode.description) && putString(output, episode.contentEncoded) &&
         putString(output, episode.link) && putString(output, episode.author) &&
         putString(output, episode.coverUrl) && putString(output, episode.chaptersUrl) &&
         putString(output, episode.chaptersType) && putString(output, episode.timelinePacked);
}

template <typename Reader>
bool getEpisode(Reader& input, PodcastEpisode& episode) {
  if (!getString(input, episode.channelId, 32) || !getString(input, episode.title, 512) ||
      !getString(input, episode.guid, 512) || !getString(input, episode.pubDate, 128) ||
      !getString(input, episode.enclosureUrl, 2048) || !getString(input, episode.enclosureType, 96) ||
      !get64(input, episode.enclosureLength) || !get32(input, episode.durationSeconds) ||
      !getString(input, episode.description) || !getString(input, episode.contentEncoded) ||
      !getString(input, episode.link, 1024) || !getString(input, episode.author, 256) ||
      !getString(input, episode.coverUrl, 1024) || !getString(input, episode.chaptersUrl, 2048) ||
      !getString(input, episode.chaptersType, 96) ||
      !getString(input, episode.timelinePacked, 32768)) return false;
  episode.timelineParsed = episode.timelinePacked.empty() && episode.chaptersUrl.empty();
  return true;
}

template <typename Writer>
bool encodeTo(Writer& output, const std::vector<PodcastEpisode>& episodes) {
  if (episodes.size() > std::numeric_limits<uint32_t>::max()) return false;
  if (!output.write(kMagic, kMagicSize) ||
      !put32(output, static_cast<uint32_t>(episodes.size()))) return false;
  for (const auto& episode : episodes) if (!putEpisode(output, episode)) return false;
  return true;
}

template <typename Reader>
bool decodeFrom(Reader& input, std::vector<PodcastEpisode>& episodes, size_t maxEpisodes) {
  episodes.clear();
  char magic[kMagicSize];
  if (!input.read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(magic)) != 0) return false;
  uint32_t count = 0;
  if (!get32(input, count) || count == 0 || count > maxEpisodes) return false;
  std::vector<PodcastEpisode> decoded;
  decoded.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    PodcastEpisode episode;
    if (!getEpisode(input, episode)) return false;
    decoded.push_back(std::move(episode));
  }
  if (input.remaining() != 0) return false;
  episodes = std::move(decoded);
  return true;
}
}  // namespace

std::string CacheCodec::encodeEpisodes(const std::vector<PodcastEpisode>& episodes) {
  std::string output;
  output.reserve(encodedSize(episodes));
  StringWriter writer(output);
  if (!encodeTo(writer, episodes)) output.clear();
  return output;
}

bool CacheCodec::decodeEpisodes(const std::string& bytes,
                                std::vector<PodcastEpisode>& episodes,
                                size_t maxEpisodes) {
  StringReader reader(bytes);
  return decodeFrom(reader, episodes, maxEpisodes);
}

#if !defined(GPP_NATIVE_TEST) && !defined(ESP_PLATFORM)
bool CacheCodec::encodeEpisodes(Print& output,
                                const std::vector<PodcastEpisode>& episodes,
                                size_t* bytesWritten) {
  PrintWriter writer(output);
  const bool ok = encodeTo(writer, episodes);
  if (bytesWritten) *bytesWritten = writer.written();
  return ok;
}

bool CacheCodec::decodeEpisodes(Stream& input, size_t byteLength,
                                std::vector<PodcastEpisode>& episodes,
                                size_t maxEpisodes) {
  StreamReader reader(input, byteLength);
  return decodeFrom(reader, episodes, maxEpisodes);
}
#endif

size_t CacheCodec::encodedSize(const std::vector<PodcastEpisode>& episodes) {
  size_t size = kMagicSize + 4;
  for (const auto& episode : episodes) {
    const std::string* fields[] = {
        &episode.channelId, &episode.title, &episode.guid, &episode.pubDate,
        &episode.enclosureUrl, &episode.enclosureType, &episode.description,
        &episode.contentEncoded, &episode.link, &episode.author, &episode.coverUrl,
        &episode.chaptersUrl, &episode.chaptersType, &episode.timelinePacked};
    size += 8 + 4;  // enclosure length + duration
    for (const auto* field : fields) size += 4 + field->size();
  }
  return size;
}

bool CacheCodec::supportedHeader(const char* data, size_t length) {
  return data && length >= kMagicSize && std::memcmp(data, kMagic, kMagicSize) == 0;
}
}  // namespace gpp
