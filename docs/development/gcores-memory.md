**English** · [简体中文](gcores-memory.zh_CN.md)

# Memory budget and resource lifecycle

ESP32-C3 has no PSRAM on this target. Internal RAM must cover the display, LVGL,
task stacks, Wi-Fi/TLS, catalog and active decoder. Free total heap, largest
contiguous block and stack watermarks are distinct measurements.

## Current allocation budget

| Resource | Budget and ownership |
| --- | --- |
| LVGL | Fixed 32 KiB pool; only the current page's controls are allocated |
| LCD transfer | Single 20-line RGB565 DMA buffer: 9,600 bytes |
| I2S | Playback-only TX; six 240-frame DMA segments, 5,760 bytes at initial stereo 16-bit format |
| Audio workers | HTTP stack 8 KiB; decode stack 12 KiB |
| App task | 6 KiB stack |
| Catalog worker | Temporary 10 KiB stack; externally deleted after suspension |
| Encoded FIFO | 12 KiB with 8 KiB startup prefill |
| Decoder input / PCM | 3 KiB input; 4,608-byte PCM, growing only as required to 8 KiB |
| HTTP | 1 KiB network scratch, 1 KiB RX and 1 KiB TX buffers |
| TLS | Dynamic incoming 16 KiB / outgoing 4 KiB record buffers |
| Catalog | Five records per each of five channels; packed chapters at most 80 / 6 KiB |
| Decoder admission | MP3: 64 KiB free internal heap; AAC, M4A and WAV: 80 KiB |

Budgets exclude allocator overhead, driver descriptors, sockets, protocol state
and codec/parser allocations. A fixed LVGL pool's unused space is not returned
to the system heap. Fonts and artwork are const data mapped from Flash; their
file sizes must not be counted as framebuffer RAM.

## Ownership and concurrency

- The player initializes only I2S TX; unused RX DMA is not allocated. Capture
  remains supported through the separate full-duplex BSP API.
- LVGL owns displayed strings. Unchanged text updates retain scroll offsets;
  controls and footer children are deleted with their page. Immutable font
  descriptors are referenced directly; only modified fallback descriptors copy.
- RSS parsing borrows bounded item/tag/CDATA views until the buffer is consumed.
  Stored episode fields own their data. The entity table is immutable.
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
