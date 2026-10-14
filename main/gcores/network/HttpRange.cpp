/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#include "HttpRange.h"

#include <cctype>
#include <limits>

namespace gpp {
namespace {

void skipSpaces(const std::string& value, size_t& cursor) {
  while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])))
    ++cursor;
}

bool number(const std::string& value, size_t& cursor, uint64_t& result) {
  skipSpaces(value, cursor);
  if (cursor >= value.size() || !std::isdigit(static_cast<unsigned char>(value[cursor])))
    return false;
  result = 0;
  while (cursor < value.size() && std::isdigit(static_cast<unsigned char>(value[cursor]))) {
    const unsigned digit = static_cast<unsigned>(value[cursor++] - '0');
    if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
    result = result * 10 + digit;
  }
  return true;
}

bool character(const std::string& value, size_t& cursor, char expected) {
  skipSpaces(value, cursor);
  if (cursor >= value.size() || value[cursor] != expected) return false;
  ++cursor;
  return true;
}

bool finished(const std::string& value, size_t cursor) {
  skipSpaces(value, cursor);
  return cursor == value.size();
}

}  // namespace

HttpContentRange parseHttpContentRange(const std::string& value) {
  HttpContentRange output;
  size_t cursor = 0;
  skipSpaces(value, cursor);
  constexpr char prefix[] = "bytes";
  for (size_t index = 0; index < sizeof(prefix) - 1; ++index) {
    if (cursor >= value.size() ||
        std::tolower(static_cast<unsigned char>(value[cursor++])) != prefix[index]) return output;
  }
  if (cursor >= value.size() || !std::isspace(static_cast<unsigned char>(value[cursor])))
    return output;
  skipSpaces(value, cursor);

  if (cursor < value.size() && value[cursor] == '*') {
    ++cursor;
    if (!character(value, cursor, '/') || !number(value, cursor, output.total) ||
        !finished(value, cursor) || output.total == 0) return HttpContentRange{};
    output.valid = true;
    output.unsatisfied = true;
    output.totalKnown = true;
    return output;
  }

  if (!number(value, cursor, output.start) || !character(value, cursor, '-') ||
      !number(value, cursor, output.end) || output.end < output.start ||
      !character(value, cursor, '/')) return HttpContentRange{};
  skipSpaces(value, cursor);
  if (cursor < value.size() && value[cursor] == '*') {
    ++cursor;
  } else {
    if (!number(value, cursor, output.total) || output.total == 0 || output.end >= output.total)
      return HttpContentRange{};
    output.totalKnown = true;
  }
  if (!finished(value, cursor)) return HttpContentRange{};
  output.valid = true;
  return output;
}

}  // namespace gpp
