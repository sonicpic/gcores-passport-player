/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#include "RssParser.h"
#include "TextUtils.h"
#include "../timeline/TimelineParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace gpp {
namespace {
constexpr size_t kMaxItemBytes = 24576;
constexpr size_t kMaxPrefixBytes = 8192;

std::string tag(std::string_view xml, const std::string& name, size_t maxBytes) {
  const std::string open = "<" + name;
  size_t a = xml.find(open);
  if (a == std::string::npos) return {};
  a = xml.find('>', a + open.size());
  if (a == std::string::npos) return {};
  ++a;
  const std::string close = "</" + name + ">";
  const size_t b = xml.find(close, a);
  if (b == std::string::npos) return {};
  return text::xmlText(xml.substr(a, std::min(b - a, maxBytes + 32)), maxBytes);
}

std::string firstTag(std::string_view xml, const std::initializer_list<const char*>& names, size_t maxBytes) {
  for (const char* name : names) { std::string v = tag(xml, name, maxBytes); if (!v.empty()) return v; }
  return {};
}

std::string_view element(std::string_view xml, const std::string& name) {
  const size_t a = xml.find("<" + name);
  if (a == std::string::npos) return {};
  const size_t b = xml.find('>', a);
  return b == std::string::npos ? std::string_view{} : xml.substr(a, b - a + 1);
}

std::string attr(std::string_view el, const std::string& name, size_t maxBytes) {
  size_t p = el.find(name);
  if (p == std::string::npos) return {};
  p += name.size();
  while (p < el.size() && std::isspace(static_cast<unsigned char>(el[p]))) ++p;
  if (p >= el.size() || el[p++] != '=') return {};
  while (p < el.size() && std::isspace(static_cast<unsigned char>(el[p]))) ++p;
  if (p >= el.size() || (el[p] != '\'' && el[p] != '"')) return {};
  const char q = el[p++];
  const size_t end = el.find(q, p);
  if (end == std::string::npos) return {};
  return text::limitUtf8(text::decodeEntities(el.substr(p, end - p)), maxBytes);
}
}

RssParser::RssParser(size_t limit, bool parseTimeline) : limit_(std::max<size_t>(1, std::min<size_t>(limit, 20))), parseTimeline_(parseTimeline) {
  buffer_.reserve(8192);
  episodes_.reserve(limit_);
}

bool RssParser::feed(const char* data, size_t length) {
  if (!data || length == 0 || episodes_.size() >= limit_) return true;
  // Bound transient memory even when a transport hands us a whole document.
  for (size_t p = 0; p < length && !complete(); p += 1024) {
    buffer_.append(data + p, std::min<size_t>(1024, length - p));
    consume();
  }
  return true;
}

void RssParser::consume() {
  while (episodes_.size() < limit_) {
    if (skippingOversize_) {
      const size_t end = buffer_.find("</item>");
      if (end == std::string::npos) { if (buffer_.size() > 64) buffer_.erase(0, buffer_.size() - 64); return; }
      buffer_.erase(0, end + 7); skippingOversize_ = false; continue;
    }
    const size_t start = buffer_.find("<item");
    if (start == std::string::npos) {
      const size_t flush = buffer_.size() > 16 ? buffer_.size() - 16 : 0;
      if (flush && channelPrefix_.size() < kMaxPrefixBytes) channelPrefix_.append(buffer_.data(), std::min(flush, kMaxPrefixBytes - channelPrefix_.size()));
      if (flush) buffer_.erase(0, flush);
      return;
    }
    if (channelPrefix_.size() < kMaxPrefixBytes) channelPrefix_.append(buffer_.data(), std::min(start, kMaxPrefixBytes - channelPrefix_.size()));
    if (start > 0) buffer_.erase(0, start);
    const size_t end = buffer_.find("</item>");
    if (end == std::string::npos) {
      if (buffer_.size() > kMaxItemBytes) { truncatedItem_ = true; skippingOversize_ = true; buffer_.erase(0, buffer_.size() - 64); }
      return;
    }
    const size_t itemLength = end + 7;
    // parseItem only borrows these bytes; every retained field owns its result.
    // Do not erase or append to buffer_ until parsing has returned.
    if (itemLength <= kMaxItemBytes) episodes_.push_back(parseItem(std::string_view(buffer_.data(), itemLength)));
    else truncatedItem_ = true;
    buffer_.erase(0, itemLength);
  }
  buffer_.clear();
}

uint32_t RssParser::parseDuration(const std::string& value) {
  const std::string s = text::trim(value);
  if (s.empty()) return 0;
  size_t p1 = s.find(':');
  if (p1 == std::string::npos) return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10));
  uint32_t seconds = 0;
  return TimelineParser::parseTime(s, seconds) ? seconds : 0;
}

PodcastEpisode RssParser::parseItem(std::string_view item) {
  PodcastEpisode e;
  e.title = firstTag(item, {"title"}, 384);
  e.guid = firstTag(item, {"guid"}, 256);
  e.pubDate = firstTag(item, {"pubDate", "dc:date"}, 96);
  e.link = firstTag(item, {"link"}, 512);
  const std::string_view enc = element(item, "enclosure");
  e.enclosureUrl = attr(enc, "url", 1024);
  e.enclosureType = attr(enc, "type", 64);
  const std::string length = attr(enc, "length", 32);
  if (!length.empty()) e.enclosureLength = std::strtoull(length.c_str(), nullptr, 10);
  if (e.guid.empty()) e.guid = !e.link.empty() ? e.link : e.enclosureUrl;
  const bool needsTimeline = parseTimeline_ && (timelineGuid_.empty() || e.guid == timelineGuid_);
  std::string descriptionHtml = firstTag(item, {"description", "itunes:summary"}, 12288);
  // Metadata refresh does not need a duplicate content:encoded HTML body.
  std::string contentHtml = needsTimeline || descriptionHtml.empty()
      ? firstTag(item, {"content:encoded"}, 16384) : std::string{};
  e.author = firstTag(item, {"itunes:author", "dc:creator", "author"}, 192);
  e.durationSeconds = parseDuration(firstTag(item, {"itunes:duration"}, 32));
  std::vector<TimelineEntry> timeline;
  if (needsTimeline) {
    if (!contentHtml.empty()) timeline = TimelineParser::parseText(contentHtml, e.durationSeconds);
    if (timeline.empty() && !descriptionHtml.empty()) timeline = TimelineParser::parseText(descriptionHtml, e.durationSeconds);
  }
  e.timelinePacked = TimelineParser::pack(timeline);
  e.description = text::plainSummary(descriptionHtml.empty() ? contentHtml : descriptionHtml, 384);
  const std::string_view image = element(item, "itunes:image");
  e.coverUrl = attr(image, "href", 512);
  const std::string_view chapters = element(item, "podcast:chapters");
  e.chaptersUrl = attr(chapters, "url", 1024);
  e.chaptersType = attr(chapters, "type", 64);
  e.timelineParsed = e.timelinePacked.empty() && e.chaptersUrl.empty();
  return e;
}

RssParseResult RssParser::finish() {
  consume();
  RssParseResult result;
  result.channelTitle = tag(channelPrefix_, "title", 192);
  result.episodes = std::move(episodes_);
  result.truncatedItem = truncatedItem_;
  result.ok = !result.episodes.empty();
  if (!result.ok) result.error = "RSS 中没有可用节目，可能是格式不完整";
  return result;
}

RssParseResult RssParser::parseDocument(const std::string& xml, size_t limit, size_t chunkSize) {
  RssParser parser(limit);
  for (size_t p = 0; p < xml.size(); p += chunkSize) parser.feed(xml.data() + p, std::min(chunkSize, xml.size() - p));
  return parser.finish();
}
}
