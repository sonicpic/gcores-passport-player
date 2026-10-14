# Audio streaming and lifecycle

[简体中文](gcores-audio.zh_CN.md)

The player streams compressed audio into a bounded FIFO, decodes it on the
ESP32-C3, and sends mono PCM to the ES8311. It does not download a complete episode
or keep a growing seek index in RAM. The C3 has no PSRAM: audio, TLS, Wi-Fi and the
UI share internal SRAM. See the [memory architecture](gcores-memory.md).

## Ownership and API contract

[`passport_audio.h`](../../main/passport_audio.h) is the application interface.
Initialize it once from the lifecycle task before command producers start. The
HTTP worker owns transport; the decode worker alone accesses the audio BSP.
Workers expose copied status snapshots and retain no UI objects or callbacks.

`play`, `pause`, `stop`, `seek` and `set_volume` enqueue commands and return before
the operation finishes. The eight-slot queue returns `ESP_ERR_TIMEOUT` when full.
`play` validates the HTTP/HTTPS scheme and copies a URL of at most 1023 bytes;
callers may release their input after the call. Volume is clamped to 0–100 percent.
Acceptance does not establish playback success: poll `passport_audio_get_status()`
for state and error details.

States are `IDLE`, `BUFFERING`, `PLAYING`, `PAUSED`, `ENDED` and `ERROR`. After
`stop`, wait until `passport_audio_is_quiescent()` returns true before system
sleep or catalog network work that requires idle audio resources. A paused stream
retains its decoder, buffers and network request and is not quiescent. External
BSP sleep, wake and format operations must not race the audio subsystem.

## Supported media

| Media | Supported representation |
|---|---|
| MP3 | MPEG Layer III, including CBR and VBR |
| AAC | ADTS AAC-LC |
| M4A | AAC-LC or MP3; `moov` must precede `mdat` (faststart) |
| WAV | 16-bit PCM |

Decoder output must be 16-bit PCM, mono or stereo, at 8–48 kHz. Stereo samples
are averaged using 32-bit arithmetic to produce mono, preserving the source
sample rate. A sample-rate change within a stream is rejected. HE-AAC/SBR, ALAC,
Opus/Ogg, FLAC and encrypted media are unsupported. Generic HLS is unsupported;
station mode supports the bounded Gcores FM format described below. The forward-only parser
cannot handle non-faststart M4A.

### Gcores FM compatibility

The website's FM is a synchronized, finite HLS playlist rather than a live
capture. The public `gapi/v1/radios` API selects the latest item with
`filter[is-fm]=1`, `page[limit]=1` and `sort=-published-at`. Its detail endpoint
with `include=media` supplies `media.attributes.audio`, currently an `.m3u8`.
The web player uses `(current time - published-at) modulo duration`, disables
user seeking, and resynchronizes when playback resumes.

Station mode fetches the latest API item on start or resume and plays its
unencrypted media playlist, MPEG-TS and AAC-LC. The manifest is streamed to find
the synchronized position and retain only three segment references. TS packets,
PAT/PMT/PES and ADTS are parsed incrementally. Compressed AAC frames before the
initial position are discarded before decoding. The decoder stays alive across
segment boundaries; completion of one HTTP resource does not end the station.

Only one TLS connection is open. API JSON is capped at 12 KiB, and its HTTP
connection is freed before JSON-tree parsing; temporary metadata buffers are freed before AAC
startup. The approximately 4.3 KiB network context uses unions for manifest/TS
state and request/RX scratch. The initial manifest is fully released before a
worker handshake admits AAC with the existing 80 KiB free-heap threshold.
The public playlist and segments use HTTP only on the official
`alioss.gcores.com/uploads/fm_programs/` path; API metadata retains verified HTTPS.
This creator-approved choice removes segment TLS allocation at the cost of
media transport encryption and integrity protection. Other hosts remain HTTPS.
No full playlist or audio segment is buffered. Encryption,
master playlists, discontinuities and unsupported formats fail safely. A broken
segment terminates the session rather than splicing damaged AAC; OK retries at
the current station time. Valid internet time is preferred; a bounded clock wait
falls back to the beginning if time remains unknown.

The LVGL pool is 28 KiB, checked against the native long-text and page-lifecycle
stress tests. TLS retains a 16 KiB receive limit for server records and uses a
1 KiB outgoing fragment limit. This releases 7 KiB relative to the 32 KiB UI
pool and 4 KiB TLS output limit. HTTP/decode stacks are 6/8 KiB and dynamic
Wi-Fi RX/TX counts are 6/6; the audio FIFO remains
unchanged. AAC and TLS must still be measured together on the device.

FM has volume, pause, resynchronized resume and screen-off playback; ordinary
seek, progress/timeline widgets and per-episode history are disabled. Entering FM
saves the previous podcast position in the worker. Obsolete timeline memory is
released before startup. API/decoder/segment heap snapshots support on-device
validation, which must establish actual AAC/TLS headroom and continuous output.

The dependency is Espressif `esp_audio_codec` 2.6.2 with the required ESP32-C3
archives and headers. Its [modified MIT license](../../components/esp_audio_codec/LICENSE)
restricts use to Espressif products. Vendor allocations remain part of the budget.

## Bounded resources

| Resource | Allocation or bound |
|---|---|
| HTTP worker stack | 6144 bytes, persistent |
| Decode worker stack | 8192 bytes, persistent |
| Encoded FIFO | 12 KiB, persistent |
| Initial stream prefill | 8 KiB, unless EOF or an error arrives earlier |
| Decoder input | 3072 bytes, allocated for playback |
| PCM output | 4608 bytes initially; one checked growth to 8192 bytes |
| HTTP scratch | 1024 bytes per request; released after completion/cancellation |
| HTTP client RX / TX buffers | 1024 / 1024 bytes, in addition to TLS allocations |
| Header scan before first PCM | 256 KiB maximum encoded input |
| MP3 seek metadata | Less than 128 bytes; no growing sparse index |

Initial PCM holds 1152 stereo 16-bit MP3 samples. Growth to the 8 KiB ceiling is
allowed only when the decoder requests more space, consumes no input and produces
no output. The same input is then retried. Larger requirements or ambiguous
partial-consumption shortages fail instead of risking dropped or repeated audio.

Decoder opening requires at least 64 KiB free internal heap for MP3 and 80 KiB
for AAC, M4A or WAV. These admission checks do not guarantee vendor allocation:
total free heap does not guarantee a sufficiently large contiguous block. The
MP3 core uses about 27.2 KiB before parser, wrapper, transport and allocator
costs. Keep catalog TLS work outside active playback; do not lower thresholds
based only on codec-core size.

## HTTP identity and recovery

HTTPS uses the ESP-IDF certificate bundle to verify certificates and hostnames;
valid system time is required. Plain HTTP is accepted for public feeds that
provide it. Redirects allow at most five hops, rejecting non-HTTP schemes and
HTTPS-to-HTTP downgrade. Requests send `Accept-Encoding: identity`.

The socket timeout is 3000 ms, with at most two read-recovery attempts. Resume
requires a quoted strong ETag; weak ETags and Last-Modified are not resume
identities. A resumed response must meet all of these conditions:

- Status is 206, with the original strong ETag.
- `Content-Range` starts at the requested absolute byte and preserves the
  original whole-resource size.
- The open-ended range reaches the last resource byte.
- Known `Content-Length` equals the range length. Chunked responses retain an
  unknown length and validate delivered size at EOF.

Recovery starts at the initial Range offset plus bytes actually delivered into
the FIFO. Bytes read into scratch but not delivered do not advance that offset.
A new 200 body, changed ETag or invalid 206 body is never appended to an existing
decoder stream. See the [response validator](../../main/passport_audio_http.c).

Cancellation changes the network generation and waits at most 6500 ms. On a
timeout, the worker retains the URL the HTTP task may still use and rejects
replacement rather than freeing borrowed memory. Transport failures appear in
audio status; arbitrary failures do not imply an automatic successful restart.

## Pause and seeking

Pause preserves playback intent and the cursor while suspending codec and I2S
output. A paused seek stays silent and pending until explicit resume. The UI
previews a held gesture locally and submits one final `seek` on release; preview
updates must not fill the command queue.

`seeking`, `seek_request_id` and `seek_complete_id` distinguish accepted requests
from completed target PCM. A paused seek waits for resume. Request IDs prevent
PCM from an earlier operation from completing a newer accepted seek. Terminal
`ERROR`, `IDLE` or `ENDED` ends the pending operation. Queue acceptance alone is
not completion.

### Fast approximate MP3 seeking

Initial saved-position playback probes the initial response, then reuses the
same Range dispatch as a manual seek before opening the decoder. It adds no
audio buffer and preserves pause intent and newer queued commands.
ID3 tags up to 256 KiB are skipped incrementally within the existing 3 KiB
input buffer before the first complete MPEG frame is inspected. Metadata must describe the
full HTTP resource, including the first MPEG byte offset and an optional trailing
128-byte ID3v1 tag. Range seeking also requires a strong ETag and known full size.

Info uses average CBR byte mapping; Xing interpolates its 100-entry, 8-bit TOC.
Range starts around one second before the target. A bounded local scan confirms
three adjacent MPEG headers, then frame decoding supplies one complete frame at
a time. Output stays silent while reservoir and synthesis state warm for at
least three frames and 511 main-data bytes. Deficient warmup frames have a fixed
tolerance bound.

`position_approximate` identifies this path. CBR estimates depend on frame size,
padding and encoder delay. One Xing quantization unit is about duration / 256,
which can be tens of seconds for a long podcast; encoder TOC accuracy adds error.
Fast seeking is not sample-exact. Duration uses full metadata, never partial 206
Content-Length. Position includes the Range base and samples decoded since it.

Range identity, synchronization or decode failures cancel the request, reset
FIFO/decoder/reservoir/time base, and restart at byte zero with sequential decode
and silent sample discard. See the [MP3 seek code](../../main/passport_mp3_seek.c).

### MP3 without a positioning table

When the first complete MP3 frame has no usable Info/Xing plan, a nonzero RSS
program duration and known HTTP resource size enable proportional positioning:
`audio_offset + audio_bytes * (target - preroll) / duration`. The ID3v2 prefix
is excluded; an unknown small terminal tag may contribute a small error. This
is explicitly approximate, including for VBR: it does not claim that sampled
frames prove constant bitrate or that the displayed target is the exact content
time. Incorrect RSS duration can also shift the result. Missing/invalid duration,
missing strong ETag, rejected Range or invalid frame synchronization retain the
sequential fallback. Native Info/Xing metadata always takes precedence. The
existing input/FIFO and one-second decoder preroll are reused, with no persisted
index or additional audio allocation.

### Sequential seeking

For MP3 without a trusted Range plan, a long seek or saved-position start scans
compressed MPEG frame headers from byte zero, counting each frame's real sample
duration. VBR is counted by frames rather than an assumed constant bitrate. Only
the last approximately one second is decoded to warm the reservoir and reach the
target. The scanner retains incomplete frames in the existing input buffer,
checks commands each frame and yields periodically; it allocates no audio index.
It still reads the skipped prefix over the network, so long jumps take time.
Invalid frame structure or a changing timebase disables scanning for that attempt
and uses the decode fallback without a repeated restart loop.

Nearby forward targets within three seconds retain the live decoder, parser,
FIFO and HTTP cursor and discard samples. Other formats retain this path for
forward seeks as well; backward, ended or unknown cursor seeks restart from zero.
Formats without compressed-frame scanning can require long sequential decoding.

Discarding leaves the DAC asleep, checks commands between frames, releases the
compute lock after each codec call, and yields one scheduler tick after 20 ms
of compute. Silent decoding remains a computational load.

## BSP and power limits

`bsp_audio_init_playback()` allocates I2S TX only, retaining six DMA descriptors
with 240 frames each. The omitted initial stereo 16-bit RX DMA payload is 5760
bytes, plus driver overhead; this is not a measured total heap gain. Full-duplex
`bsp_audio_init()` remains available for capture. Upgrading active TX-only output
returns `ESP_ERR_INVALID_STATE`; format/capture changes require stopped,
serialized PCM I/O.

Playback requests ES8311 DAC-only mode. The vendor sequence can still touch ADC
and analog bias registers, so complete analog capture power-off is not
guaranteed. Pause and stop run the full suspend sequence, verify key registers
and stop I2S. The board has no MCU-controlled amplifier-enable pin; firmware
cannot cut amplifier power independently.

CPU DFS is 80–160 MHz. A maximum-frequency lock covers decoder open/process
calls and is released before FIFO, HTTP and PCM/DMA waits. Automatic light sleep
is disabled to retain native USB. Idle, playback and sleep current require
hardware measurement. Check stack watermarks under TLS redirects/recovery and
each supported codec before reducing stacks.

## Verification references

[Audio command and MP3 tests](../../tools/test_passport_audio.py) exercise command
dispatch, pause/seek intent, completion bookkeeping and bounded PCM growth with
host I/O doubles. [HTTP tests](../../tools/test_passport_audio_http.py) exercise
the production response validator. They require Python and a C11 host compiler.
Host tests do not validate vendor decoding, real TLS stack depth, audible output
or board current. Device acceptance should cover supported formats, underruns,
pause/resume, forward/backward jumps, Range rejection/fallback, stop/sleep/wake
and repeated resource recovery.

### FM programme names

A sparse `include=media.timelines` query supplies programme boundaries and titles.
Metadata is parsed incrementally before opening AAC, with a 2 KiB resource buffer,
a maximum 8 KiB packed table and 128 entries. One replaceable NVS blob caches the
table and media URL for up to 24 hours, tied to the latest station identity.
The current/next title snapshot is bounded to 384 UTF-8 bytes each. At a programme
boundary, the network worker briefly loads the table, selects the nearby names,
and frees it; no title-related TLS request runs during playback. If NVS writing
fails, a bounded packed RAM table is retained for that session. Missing metadata
falls back to audio-only playback. Names follow the initial station offset plus
actually decoded audio time, wrapping at the station duration.

The FM player uses the current programme as its large title and a scrolling
single-line next title below the upcoming-programme label. Eight decorative bars update at 4 Hz;
they do not sample audio or compute a spectrum, and stop while paused, hidden or
screen-off. They require no image framebuffer.
