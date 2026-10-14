**English** · [简体中文](gcores-memory.zh_CN.md)

# Memory budget and resource lifecycle

ESP32-C3 has no PSRAM on this target. Internal RAM must cover the display, LVGL,
task stacks, Wi-Fi/TLS, catalog and active decoder. Free total heap, largest
contiguous block and stack watermarks are distinct measurements.

## Current allocation budget

| Resource | Budget and ownership |
| --- | --- |
| LVGL | Fixed 28 KiB pool; only the current page's controls are allocated |
| LCD transfer | Single 20-line RGB565 DMA buffer: 9,600 bytes |
| I2S | Playback-only TX; six 240-frame DMA segments, 5,760 bytes at initial stereo 16-bit format |
| Audio workers | HTTP stack 6 KiB; decode stack 8 KiB |
| App task | 6 KiB stack |
| Catalog worker | Temporary 10 KiB stack; externally deleted after suspension |
| Encoded FIFO | 12 KiB with 8 KiB startup prefill |
| Decoder input / PCM | 3 KiB input; 4,608-byte PCM, growing only as required to 8 KiB |
| HTTP | 1 KiB network scratch, 1 KiB RX and 1 KiB TX buffers |
| TLS | Dynamic incoming 16 KiB / outgoing 4 KiB record buffers |
| Catalog | Up to 100 Flash records per channel; four-record RAM window plus one playback snapshot; packed chapters at most 80 / 6 KiB |
| History | 32 fixed NVS keys, 64-byte records (2,048-byte payload plus NVS overhead); scan one record at a time |
| Decoder admission | MP3: 64 KiB free internal heap; AAC, M4A and WAV: 80 KiB |

Budgets exclude allocator overhead, driver descriptors, sockets, protocol state
and codec/parser allocations. A fixed LVGL pool's unused space is not returned
to the system heap. Fonts and artwork are const data mapped from Flash; their
file sizes must not be counted as framebuffer RAM.

## Ownership and concurrency

The three dynamic CJK sizes contain exactly the 8,105 standard Chinese characters
and 512 non-Chinese characters per face. Glyphs remain uncompressed 2-bpp bitmaps,
so subsetting adds no decompression work. Characters outside this set use the
visible fallback. Supplementary-plane standard characters remain supported.
Home and Settings share only the header artwork; their body background is black.

The 3 MiB catalog partition has two fixed banks per channel. Streaming RSS
refresh writes one record at a time, validates CRCs and commits the header last.
Incomplete refreshes retain the previous bank. Old banks are erased only when
reused; the catalog does not grow with refresh count. Each feed is bounded to
100 valid episodes and a 2 MiB response budget. These limits are not a promise
that every feed exposes 100 episodes.

Catalog refresh reserves its bounded 25 KiB item buffer before opening TLS,
then parses only fields stored in the Flash record. Episode descriptions,
authors, dates and cover metadata are skipped on this path. This avoids buffer
growth while TLS is live and keeps the online allocation peak independent of
whether a feed yields five or 100 episodes. Network catalog and chapter work is
admitted only with at least 80 KiB free heap and a 32 KiB largest block; otherwise
the operation returns a recoverable low-memory error and retains the old catalog.

Per-episode progress uses a separate bounded NVS map so minute-by-minute saves
do not rewrite a catalog bank. SHA-256 identifies channel plus GUID; the compact
record stores progress, duration, completion and a save sequence with CRC.
Unchanged saves perform no write. NVS reclaims old entries within its fixed
partition, and a changed save replaces the oldest of 32 records when full.
There is no append-only history or unbounded heap collection. The independent
last-playback snapshot retains a playable URL/title even after RSS eviction;
its legacy progress is imported if no per-episode record exists.

- The player initializes only I2S TX; unused RX DMA is not allocated. Capture
  remains supported through the separate full-duplex BSP API.
- LVGL owns displayed strings. Unchanged text updates retain scroll offsets;
  controls and footer children are deleted with their page. Immutable font
  descriptors are referenced directly; only modified fallback descriptors copy.
- RSS parsing borrows bounded item/tag/CDATA views until the buffer is consumed.
  Stored episode fields own their data. Metadata refresh skips unused large
  bodies; full parsing remains available when a chapter timeline needs them.
  The entity table is immutable.
- The catalog retains only displayed/playback metadata and packed chapters.
  Refresh failures keep the previous usable list and cache.
- Catalog jobs stop or coordinate with audio as required. The app waits for the
  worker to suspend and deletes it synchronously before decoder admission;
  self-deletion would defer stack reclamation until the idle task runs.
- Stop/replace/seek cancels the old HTTP generation before FIFO reset. Old
  producers cannot append to a replacement stream.

## Safety margins

Do not reduce TLS record sizes, output DMA, FIFO or task stacks solely because
one MP3 sample passes. Supported formats and worst-case TLS/redirect/parser call
chains must retain margin. The largest free block matters even with enough total
heap. The MP3 admission gate remains conservative; low-memory failure is explicit.

Measure free/minimum/largest internal heap and each task's own stack high-water
mark at transitions. Numeric resource diagnostics omit URLs and credentials.
Compile-time size reports describe static DRAM; native renderer measurements
describe its fixed pool and are not target allocator measurements.

See [audio lifecycle](gcores-audio.md) and [validation](gcores-validation.md).

## Saved Wi-Fi profiles

Wi-Fi keeps an optional one/two-network bank in one replaceable 220-byte NVS
blob. The bank, active profile and pending two-profile submission total 532 bytes
of fixed mutable profile data, plus small connection-state/timing fields. It uses
the existing application poll and network events rather than a new task. The
management page is rendered in a temporary approximately 2 KiB allocation and
freed after sending; credentials are never included in its response. Driver
configuration uses RAM storage after importing the former Flash configuration,
so failover does not rewrite credentials each attempt. Stable successful
reconnections do not rewrite unchanged saved profiles. Offline attempts back off
from one minute to at most five minutes; connected operation does not periodically
scan. Stored credentials remain unencrypted with this firmware configuration.
