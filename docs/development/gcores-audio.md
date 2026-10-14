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
Opus/Ogg, FLAC, HLS and encrypted media are unsupported. The forward-only parser
cannot handle non-faststart M4A.

The dependency is Espressif `esp_audio_codec` 2.6.2 with the required ESP32-C3
archives and headers. Its [modified MIT license](../../components/esp_audio_codec/LICENSE)
restricts use to Espressif products. Vendor allocations remain part of the budget.

## Bounded resources

| Resource | Allocation or bound |
|---|---|
| HTTP worker stack | 8192 bytes, persistent |
| Decode worker stack | 12288 bytes, persistent |
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

Info/Xing discovery requires the first complete MPEG frame to fit the initial
3 KiB input; large ID3 tags use sequential fallback. Metadata must describe the
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

### Sequential seeking

Forward seeks retain the live decoder, parser, FIFO and HTTP cursor when trusted
fast-seek metadata is unavailable, or when the target is within three seconds of
the live cursor. Samples are discarded to the target. Backward, ended or unknown
cursor seeks without a usable fast path restart from zero. `play(url,
resume_seconds)` also starts from zero and discards to the saved position. Long
sequential jumps can take substantial time and bandwidth.

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
