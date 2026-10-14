/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#include "TimelineParser.h"
#include "../rss/TextUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>

namespace gpp {
namespace {
bool plausible(uint32_t value, uint32_t duration) {
  return duration == 0 || value <= duration + 60;
}

void normalizeColon(std::string& s) {
  const std::string fullColon = "：";
  size_t p = 0;
  while ((p = s.find(fullColon, p)) != std::string::npos) s.replace(p, fullColon.size(), ":");
}

bool consumeAt(const std::string& value, size_t& position, const char* token) {
  const size_t length = std::char_traits<char>::length(token);
  if (value.compare(position, length, token) != 0) return false;
  position += length;
  return true;
}

bool splitTimelineLine(const std::string& input, std::string& stamp, std::string& title) {
  const std::string line = text::trim(input);
  if (line.empty()) return false;
  size_t p = 0;
  if (line[p] == '[' || line[p] == '(') ++p;
  else consumeAt(line, p, "（");

  const size_t firstDigits = p;
  while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p]))) ++p;
  if (p == firstDigits || p - firstDigits > 3 || p >= line.size() || line[p] != ':') return false;
  ++p;
  const size_t secondDigits = p;
  while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p]))) ++p;
  if (p == secondDigits || p - secondDigits > 2) return false;
  if (p < line.size() && line[p] == ':') {
    ++p;
    const size_t thirdDigits = p;
    while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p]))) ++p;
    if (p == thirdDigits || p - thirdDigits > 2) return false;
  }
  stamp = line.substr(firstDigits, p - firstDigits);

  bool changed = true;
  while (p < line.size() && changed) {
    changed = false;
    while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) { ++p; changed = true; }
    if (p < line.size() && (line[p] == ']' || line[p] == ')' || line[p] == '-' || line[p] == '|' || line[p] == ':')) {
      ++p; changed = true; continue;
    }
    if (consumeAt(line, p, "）") || consumeAt(line, p, "–") || consumeAt(line, p, "—")) changed = true;
  }
  title = text::collapseWhitespace(line.substr(p));
  return true;
}

std::vector<TimelineEntry> finish(std::vector<TimelineEntry> entries, uint32_t duration, size_t maxEntries) {
  entries.erase(std::remove_if(entries.begin(), entries.end(), [duration](const auto& e){
    return e.title.empty() || !plausible(e.startSeconds, duration);
  }), entries.end());
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b){ return a.startSeconds < b.startSeconds; });
  entries.erase(std::unique(entries.begin(), entries.end(), [](const auto& a, const auto& b){
    return a.startSeconds == b.startSeconds;
  }), entries.end());
  if (entries.size() > maxEntries) entries.resize(maxEntries);
  return entries;
}

bool isBreakTag(const std::string& tag) {
  size_t p = 0;
  if (p < tag.size() && tag[p] == '/') ++p;
  return tag.compare(p, 2, "br") == 0 || tag.compare(p, 1, "p") == 0 ||
         tag.compare(p, 3, "div") == 0 || tag.compare(p, 2, "li") == 0;
}

bool parseJsonString(const std::string& json, size_t start, size_t end, std::string& value) {
  size_t p = json.find(':', start);
  if (p == std::string::npos || p >= end) return false;
  ++p;
  while (p < end && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
  if (p >= end || json[p++] != '"') return false;
  value.clear();
  value.reserve(96);
  auto unicode = [&](size_t& cursor, uint32_t& cp) {
    if (cursor + 4 > end) return false;
    cp = 0;
    for (unsigned digit = 0; digit < 4; ++digit) {
      unsigned char c = json[cursor++];
      unsigned hex = c >= '0' && c <= '9' ? c - '0' :
                     c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                     c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
      if (hex > 15) return false;
      cp = (cp << 4) | hex;
    }
    return true;
  };
  while (p < end && value.size() < 256) {
    char c = json[p++];
    if (c == '"') return true;
    if (c != '\\') {
      if (static_cast<unsigned char>(c) < 0x20) return false;
      value.push_back(c); continue;
    }
    if (p >= end) return false;
    c = json[p++];
    if (c == 'n' || c == 'r' || c == 't') value.push_back(' ');
    else if (c == '"' || c == '\\' || c == '/') value.push_back(c);
    else if (c == 'b' || c == 'f') value.push_back(' ');
    else if (c == 'u') {
      uint32_t cp;
      if (!unicode(p, cp)) return false;
      if (cp >= 0xd800 && cp <= 0xdbff) {
        if (p + 2 > end || json[p] != '\\' || json[p+1] != 'u') return false;
        p += 2;
        uint32_t low;
        if (!unicode(p, low) || low < 0xdc00 || low > 0xdfff) return false;
        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
      } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
      if (!cp) return false;
      value += text::decodeEntities("&#" + std::to_string(cp) + ";");
    } else return false;
  }
  return false;
}

size_t matchingObjectEnd(const std::string& json, size_t start) {
  unsigned depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (size_t p = start; p < json.size(); ++p) {
    const char c = json[p];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') { quoted = true; continue; }
    if (c == '{') ++depth;
    else if (c == '}' && depth && --depth == 0) return p;
  }
  return std::string::npos;
}
}

bool TimelineParser::parseTime(const std::string& value, uint32_t& seconds) {
  std::string s = text::trim(value);
  size_t first = s.find(':');
  if (first == std::string::npos) return false;
  size_t second = s.find(':', first + 1);
  auto number = [](const std::string& x, uint32_t& result) {
    if (x.empty() || x.size() > 7) return false;
    uint64_t value = 0;
    for (unsigned char c : x) {
      if (!std::isdigit(c)) return false;
      value = value * 10 + (c - '0');
      if (value > UINT32_MAX) return false;
    }
    result = static_cast<uint32_t>(value);
    return true;
  };
  std::string a = s.substr(0, first), b, c;
  uint32_t av = 0, bv = 0, cv = 0;
  if (second == std::string::npos) {
    b = s.substr(first + 1);
    if (!number(a, av) || !number(b, bv) || bv >= 60) return false;
    const uint64_t total = static_cast<uint64_t>(av) * 60 + bv;
    if (total > UINT32_MAX) return false;
    seconds = static_cast<uint32_t>(total);
  } else {
    b = s.substr(first + 1, second - first - 1); c = s.substr(second + 1);
    if (!number(a, av) || !number(b, bv) || !number(c, cv) || bv >= 60 || cv >= 60) return false;
    const uint64_t total = static_cast<uint64_t>(av) * 3600 + bv * 60 + cv;
    if (total > UINT32_MAX) return false;
    seconds = static_cast<uint32_t>(total);
  }
  return true;
}

std::vector<TimelineEntry> TimelineParser::parseText(const std::string& htmlOrText, uint32_t duration, size_t maxEntries) {
  std::vector<TimelineEntry> out;
  const size_t collectLimit = maxEntries + std::min<size_t>(maxEntries, 8);
  out.reserve(collectLimit);
  std::string line;
  line.reserve(256);
  std::string tag;
  tag.reserve(12);
  bool inTag = false;
  auto flush = [&]() {
    if (line.empty() || out.size() >= maxEntries * 2) { line.clear(); return; }
    line = text::decodeEntities(line);
    normalizeColon(line);
    std::string stamp, title;
    if (!splitTimelineLine(line, stamp, title)) { line.clear(); return; }
    uint32_t sec = 0;
    if (!parseTime(stamp, sec)) { line.clear(); return; }
    if (title.empty()) title = "章节";
    out.push_back({sec, text::limitUtf8(title, 180), {}});
    line.clear();
  };
  for (size_t i = 0; i < htmlOrText.size() && out.size() < collectLimit; ++i) {
    const char c = htmlOrText[i];
    if (!inTag && c == '<') { inTag = true; tag.clear(); continue; }
    if (inTag) {
      if (c == '>') { inTag = false; if (isBreakTag(tag)) flush(); }
      else if (tag.size() < 12 && !std::isspace(static_cast<unsigned char>(c)))
        tag.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      continue;
    }
    if (c == '\n' || c == '\r') { flush(); continue; }
    if (line.size() < 768) line.push_back(c);
  }
  flush();
  return finish(std::move(out), duration, maxEntries);
}

std::vector<TimelineEntry> TimelineParser::parsePodcastChaptersJson(const std::string& json, uint32_t duration, size_t maxEntries) {
  std::vector<TimelineEntry> out;
  const size_t collectLimit = maxEntries + std::min<size_t>(maxEntries, 8);
  out.reserve(collectLimit);
  size_t p = 0;
  while (p < json.size() && out.size() < collectLimit) {
    const size_t sk = json.find("\"startTime\"", p);
    if (sk == std::string::npos) break;
    const size_t a = json.rfind('{', sk);
    if (a == std::string::npos) break;
    const size_t b = matchingObjectEnd(json, a);
    if (b == std::string::npos) break;
    const size_t tk = json.find("\"title\"", a);
    if (sk < b && tk < b) {
      const size_t colon = json.find(':', sk + 11);
      if (colon < b) {
        char* end = nullptr;
        const double seconds = std::strtod(json.c_str() + colon + 1, &end);
        std::string title;
        if (end != json.c_str() + colon + 1 && std::isfinite(seconds) && seconds >= 0 &&
            seconds <= UINT32_MAX && parseJsonString(json, tk, b, title))
          out.push_back({static_cast<uint32_t>(seconds), text::limitUtf8(text::decodeEntities(title), 180), {}});
      }
    }
    p = sk + 11;
  }
  return finish(std::move(out), duration, maxEntries);
}

int TimelineParser::currentIndex(const std::vector<TimelineEntry>& entries, uint32_t position) {
  int result = -1;
  for (size_t i = 0; i < entries.size() && entries[i].startSeconds <= position; ++i) result = static_cast<int>(i);
  return result;
}

bool TimelineParser::currentPacked(const std::string& packed, uint32_t position,
                                   PackedTimelineEntry& result) {
  if (packed.size() < 2 || packed.size() > kMaxPackedBytes) return false;
  const size_t count = static_cast<unsigned char>(packed[0]) |
                       (static_cast<size_t>(static_cast<unsigned char>(packed[1])) << 8);
  if (!count || count > 80) return false;
  size_t cursor = 2;
  uint32_t previous = 0;
  bool found = false;
  PackedTimelineEntry selected;
  for (size_t i = 0; i < count; ++i) {
    if (packed.size() - cursor < 6) return false;
    uint32_t seconds = 0;
    for (unsigned b = 0; b < 4; ++b)
      seconds |= static_cast<uint32_t>(static_cast<unsigned char>(packed[cursor++])) << (8 * b);
    const size_t length = static_cast<unsigned char>(packed[cursor]) |
                          (static_cast<size_t>(static_cast<unsigned char>(packed[cursor+1])) << 8);
    cursor += 2;
    if (!length || length > 180 || length > packed.size() - cursor || (i && seconds < previous)) return false;
    const std::string_view title(packed.data() + cursor, length);
    if (title.find('\0') != std::string_view::npos) return false;
    if (seconds <= position) { selected = {seconds, title, i}; found = true; }
    previous = seconds;
    cursor += length;
  }
  if (cursor != packed.size() || !found) return false;
  result = selected;
  return true;
}

std::string TimelineParser::pack(const std::vector<TimelineEntry>& entries) {
  const size_t limit = std::min<size_t>(entries.size(), 80);
  if (!limit) return {};
  auto safeTitleLength = [](const std::string& title) {
    size_t length = std::min<size_t>(title.size(), 180);
    while (length && length < title.size() &&
           (static_cast<unsigned char>(title[length]) & 0xc0) == 0x80) --length;
    return length;
  };
  size_t bytes = 2;
  size_t count = 0;
  while (count < limit) {
    const size_t recordBytes = 6 + safeTitleLength(entries[count].title);
    if (recordBytes > kMaxPackedBytes - bytes) break;
    bytes += recordBytes;
    ++count;
  }
  std::string out;
  out.reserve(bytes);
  out.push_back(static_cast<char>(count & 0xff));
  out.push_back(static_cast<char>((count >> 8) & 0xff));
  for (size_t i = 0; i < count; ++i) {
    const uint32_t seconds = entries[i].startSeconds;
    for (unsigned b = 0; b < 4; ++b) out.push_back(static_cast<char>((seconds >> (8 * b)) & 0xff));
    const size_t safeLength = safeTitleLength(entries[i].title);
    const uint16_t length = static_cast<uint16_t>(safeLength);
    out.push_back(static_cast<char>(length & 0xff));
    out.push_back(static_cast<char>((length >> 8) & 0xff));
    out.append(entries[i].title.data(), length);
  }
  return out;
}

bool TimelineParser::unpack(const std::string& packed, std::vector<TimelineEntry>& entries,
                            size_t maxEntries) {
  entries.clear();
  if (packed.empty()) return true;
  if (packed.size() < 2) return false;
  size_t p = 0;
  const uint16_t count = static_cast<unsigned char>(packed[p]) |
                         (static_cast<uint16_t>(static_cast<unsigned char>(packed[p + 1])) << 8);
  p += 2;
  if (count > maxEntries || count > 80) return false;
  entries.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    if (p + 6 > packed.size()) { entries.clear(); return false; }
    uint32_t seconds = 0;
    for (unsigned b = 0; b < 4; ++b) seconds |= static_cast<uint32_t>(static_cast<unsigned char>(packed[p++])) << (8 * b);
    const uint16_t length = static_cast<unsigned char>(packed[p]) |
                            (static_cast<uint16_t>(static_cast<unsigned char>(packed[p + 1])) << 8);
    p += 2;
    if (length > 180 || p + length > packed.size()) { entries.clear(); return false; }
    TimelineEntry entry;
    entry.startSeconds = seconds;
    entry.title.assign(packed, p, length);
    p += length;
    entries.push_back(std::move(entry));
  }
  if (p != packed.size()) { entries.clear(); return false; }
  return true;
}
}
