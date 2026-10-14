/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#include "TextUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>

namespace gpp::text {
namespace {
void appendUtf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
  else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}
}

std::string trim(const std::string& input) {
  size_t a = 0, b = input.size();
  while (a < b && std::isspace(static_cast<unsigned char>(input[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(input[b - 1]))) --b;
  return input.substr(a, b - a);
}

std::string decodeEntities(std::string_view input) {
  struct NamedEntity { std::string_view name, value; };
  // Eleven immutable names need no dynamically allocated hash buckets/nodes.
  static constexpr NamedEntity named[] = {
      {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""},
      {"apos", "'"}, {"nbsp", " "}, {"ensp", " "}, {"emsp", " "},
      {"ndash", "–"}, {"mdash", "—"}, {"middot", "·"}};
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size();) {
    if (input[i] != '&') { out.push_back(input[i++]); continue; }
    const size_t semi = input.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 12) { out.push_back(input[i++]); continue; }
    const std::string_view entity = input.substr(i + 1, semi - i - 1);
    const auto it = std::find_if(std::begin(named), std::end(named), [entity](const auto& item) {
      return item.name == entity;
    });
    if (it != std::end(named)) { out.append(it->value.data(), it->value.size()); i = semi + 1; continue; }
    if (!entity.empty() && entity[0] == '#') {
      char* end = nullptr;
      const bool hex = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
      // Keep strtoul's established permissive handling, with a bounded NUL
      // terminated numeric token rather than reading beyond the borrowed view.
      char token[13];
      std::copy(entity.begin(), entity.end(), token);
      token[entity.size()] = '\0';
      const char* start = token + (hex ? 2 : 1);
      const unsigned long value = std::strtoul(start, &end, hex ? 16 : 10);
      if (end && *end == '\0' && value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF)) {
        appendUtf8(out, static_cast<uint32_t>(value)); i = semi + 1; continue;
      }
    }
    out.append(input.data() + i, semi - i + 1); i = semi + 1;
  }
  return out;
}

std::string stripHtml(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool tag = false;
  std::string tagName;
  for (size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (!tag && c == '<') { tag = true; tagName.clear(); continue; }
    if (tag) {
      if (c == '>') {
        tag = false;
        std::string low = tagName;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char x){ return static_cast<char>(std::tolower(x)); });
        if (low.rfind("br", 0) == 0 || low.rfind("/p", 0) == 0 || low.rfind("/div", 0) == 0 || low.rfind("/li", 0) == 0) out.push_back('\n');
      } else if (tagName.size() < 12 && !std::isspace(static_cast<unsigned char>(c))) tagName.push_back(c);
      continue;
    }
    out.push_back(c);
  }
  return decodeEntities(out);
}

std::string collapseWhitespace(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool space = false;
  for (unsigned char c : input) {
    if (c == '\r') continue;
    if (c == '\n') {
      while (!out.empty() && out.back() == ' ') out.pop_back();
      if (!out.empty() && out.back() != '\n') out.push_back('\n');
      space = false;
    } else if (c == '\t' || c == ' ') {
      if (!space && !out.empty() && out.back() != '\n') out.push_back(' ');
      space = true;
    } else {
      out.push_back(static_cast<char>(c)); space = false;
    }
  }
  return trim(out);
}

std::string limitUtf8(const std::string& input, size_t maxBytes) {
  if (input.size() <= maxBytes) return input;
  size_t n = maxBytes;
  while (n > 0 && (static_cast<unsigned char>(input[n]) & 0xC0) == 0x80) --n;
  return input.substr(0, n);
}

std::string xmlText(std::string_view input, size_t maxBytes) {
  size_t first = 0, last = input.size();
  while (first < last && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
  while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
  input = input.substr(first, last - first);
  constexpr std::string_view cdataStart = "<![CDATA[";
  if (input.rfind(cdataStart, 0) == 0) {
    input.remove_prefix(cdataStart.size());
    const size_t end = input.rfind("]]>");
    if (end != std::string_view::npos) input = input.substr(0, end);
  }
  return limitUtf8(decodeEntities(input), maxBytes);
}

std::string plainSummary(const std::string& input, size_t maxBytes) {
  if (!maxBytes) return {};
  std::string raw;
  raw.reserve(std::min(input.size(), maxBytes + 96));
  bool inTag = false;
  bool pendingSpace = false;
  for (size_t i = 0; i < input.size() && raw.size() < maxBytes + 64; ++i) {
    const char c = input[i];
    if (!inTag && c == '<') { inTag = true; pendingSpace = true; continue; }
    if (inTag) { if (c == '>') inTag = false; continue; }
    if (std::isspace(static_cast<unsigned char>(c))) { pendingSpace = true; continue; }
    if (pendingSpace && !raw.empty()) raw.push_back(' ');
    pendingSpace = false;
    raw.push_back(c);
  }
  return limitUtf8(trim(decodeEntities(raw)), maxBytes);
}
}
