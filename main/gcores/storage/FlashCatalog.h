/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <cstddef>
#include <cstdint>

namespace gpp {
/* Byte-defined wire record: no host ABI, pointer, native integer or padding.
 * The zero-terminated original identity is compared in full, never by hash. */
struct FlashCatalogRecord {
  char title[385];
  char guid[257];
  char url[1025];
  char chaptersUrl[1025];
  uint8_t duration[4];
  uint8_t enclosureLength[8];
  uint8_t reserved[108];
  uint8_t checksum[4];
};
static_assert(sizeof(FlashCatalogRecord) == 2816);

struct FlashCatalogIo {
  void *context = nullptr;
  size_t bytes = 0;
  bool (*read)(void *, size_t, void *, size_t) = nullptr;
  bool (*write)(void *, size_t, const void *, size_t) = nullptr;
  bool (*erase)(void *, size_t, size_t) = nullptr;
};

/* Callers serialize operations. The store never allocates, performs network
 * I/O, or holds any full catalog in RAM. One refresh transaction at a time.
 * Old committed A/B slot remains readable until verified final marker write. */
class FlashCatalog {
 public:
  static constexpr size_t kChannels = 5, kMaxEpisodes = 100;
  static constexpr size_t kSlotBytes = 0x4c000, kHeaderBytes = 4096;
  static constexpr size_t kRequiredBytes = kChannels * 2 * kSlotBytes;
  bool mount(const FlashCatalogIo& io);
  size_t count(size_t channel) const;
  bool read(size_t channel, size_t index, FlashCatalogRecord& record) const;
  int find(size_t channel, const char *guid, const char *url) const;
  int find(size_t channel, const char *guid, const char *url, FlashCatalogRecord& scratch) const;
  bool begin(size_t channel);
  bool append(const FlashCatalogRecord& record);
  bool commit();
  void abort();
  bool available() const { return available_; }
  static uint32_t get32(const uint8_t *bytes);
  static uint64_t get64(const uint8_t *bytes);
  static void put32(uint8_t *bytes, uint32_t value);
  static void put64(uint8_t *bytes, uint64_t value);
  static void seal(FlashCatalogRecord& record);
 private:
  struct State { uint32_t generation = 0; uint16_t count = 0; int8_t slot = -1; };
  FlashCatalogIo io_{};
  State states_[kChannels]{};
  bool available_ = false, writing_ = false, failed_ = false;
  size_t writingChannel_ = 0, writingSlot_ = 0, written_ = 0;
  uint32_t bodyCrc_ = UINT32_MAX;
  size_t offset(size_t channel, size_t slot) const;
  bool validate(size_t channel, size_t slot, State& state) const;
  static bool validRecord(const FlashCatalogRecord& record);
};
}
