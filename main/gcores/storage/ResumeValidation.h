/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace gpp {
inline bool validResumeIdentity(uint8_t channel, uint8_t episode, uint8_t completed,
    const char *guid, size_t guid_size, const char *url, size_t url_size,
    const char *title, size_t title_size) {
    if (channel >= 5 || (episode > 100 && episode != UINT8_MAX) || completed > 1 ||
        !guid || !url || !title || !guid_size || !url_size || !title_size ||
        !std::memchr(guid, 0, guid_size) || !std::memchr(url, 0, url_size) ||
        !std::memchr(title, 0, title_size) || !guid[0] || !title[0]) return false;
    // Store the original RSS identity, including HTTP URLs accepted by playback.
    // This validation does not change transport TLS or redirect policy.
    size_t scheme = std::strncmp(url, "https://", 8) == 0 ? 8 :
                    std::strncmp(url, "http://", 7) == 0 ? 7 : 0;
    if (!scheme || std::strlen(url) <= scheme || url[scheme] == '/') return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(url); *p; ++p)
        if (*p <= 0x20 || *p == 0x7f) return false;
    return true;
}
}
