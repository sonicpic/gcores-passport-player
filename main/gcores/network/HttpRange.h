/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstdint>
#include <string>

namespace gpp {

struct HttpContentRange {
  bool valid = false;
  bool unsatisfied = false;
  bool totalKnown = false;
  uint64_t start = 0;
  uint64_t end = 0;
  uint64_t total = 0;
};

// Strictly parses the two RFC 9110 byte-range response forms:
// "bytes first-last/complete" and "bytes */complete".
HttpContentRange parseHttpContentRange(const std::string& value);

}  // namespace gpp
