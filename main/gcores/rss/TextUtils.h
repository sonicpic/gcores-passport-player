/* SPDX-License-Identifier: GPL-3.0-only; upstream sonicpic/gcores-pocket-player */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace gpp::text {
std::string decodeEntities(std::string_view input);
std::string stripHtml(const std::string& input);
std::string trim(const std::string& input);
std::string collapseWhitespace(const std::string& input);
std::string limitUtf8(const std::string& input, size_t maxBytes);
std::string xmlText(std::string_view input, size_t maxBytes);
// Builds a short readable summary without ever materialising the complete
// HTML document as a second large string.
std::string plainSummary(const std::string& input, size_t maxBytes);
}
