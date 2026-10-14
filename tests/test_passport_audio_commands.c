/* The generated headers contain actual passport_audio.c types and dispatch.
 * Only queue/network/BSP operations are doubled; paused decode gating is real. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "passport_audio.h"
#include "passport_mp3_seek.h"
#include "audio_command_types.h"

#include "audio_buffer_constants.h"
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))

static int s_commands=1, s_lock;
static passport_audio_status_t s_status;
static char *url;
static uint8_t *input, *pcm;
static void *decoder;
static size_t input_length, input_offset;
static size_t pcm_bytes;
static uint64_t decoded_samples, discard_samples, scanned;
static uint64_t position_base_samples;
static passport_mp3_seek_t mp3_seek;
static passport_mp3_seek_plan_t seek_plan;
static passport_mp3_frame_t range_frame;
static bool range_mode, range_synced;
static uint32_t range_scan_bytes, warm_frames, warm_bytes, s_seek_active_id;
static uint32_t target_seconds, format_hz, episode_duration;
static int64_t discard_yield_at, clock_us;
static int64_t seek_started_us;
static bool seek_output_pending;
static bool resume_probe_pending, resume_seek_ready;
static bool mp3_probe_done, mp3_scanning, mp3_scan_disabled;
static passport_mp3_prefix_t mp3_prefix;
static passport_mp3_scan_t mp3_scan;
static uint64_t mp3_scan_bytes;
static int64_t mp3_scan_log_at;
static bool playing_fm,s_network_fm;
static bool running, paused, codec_awake, have_output, eos_flushed;
static bool quiet;
static command_t queued;
static bool pending;
static int network_starts, network_cancels, decoder_closes, decode_ticks;
static esp_err_t cancel_result = ESP_OK, start_result = ESP_OK, sleep_result = ESP_OK;
static net_status_t net;
static net_status_t network_status(void) { return net; }
static int64_t esp_timer_get_time(void) { return clock_us; }
static int yield_calls, format_calls, pcm_samples, first_pcm;
static void vTaskDelay(int ticks) { assert(ticks==1); ++yield_calls; clock_us+=10000; }
static esp_err_t bsp_audio_set_playback_format(uint32_t hz, int bits, int channels) {
    assert(hz>=8000 && hz<=48000 && bits==16 && channels==1); ++format_calls; return ESP_OK;
}
static esp_err_t bsp_audio_write(const void *buffer, size_t bytes) {
    assert(bytes && bytes<=960 && bytes%2==0);
    if (!pcm_samples) first_pcm=*(const int16_t *)buffer;
    pcm_samples+=bytes/2; return ESP_OK;
}
static void publish(passport_audio_state_t state, esp_err_t error);
static void failure_log(const char *stage, int error);
#define ESP_LOGI(...) ((void)0)

static bool realloc_fails;
static unsigned realloc_calls;
static void *test_realloc(void *buffer, size_t bytes) {
    ++realloc_calls;
    return realloc_fails ? NULL : realloc(buffer, bytes);
}
#define realloc test_realloc
#include "audio_pcm_grow.h"
#undef realloc

// Compile the real downmix/discard/PCM/yield branch, without a codec mock.
static void decoded_frame(uint32_t hz, unsigned channels, size_t samples) {
    struct { uint32_t sample_rate; unsigned channel; } info={hz,channels};
    struct { size_t decoded_size; } out={samples*channels*2};
    for (int once=0;once<1;++once) {
#include "audio_pcm_output.h"
    }
    return;
failed:
    assert(!running && s_status.state==PASSPORT_AUDIO_ERROR);
}

static int xQueueReceive(int queue, command_t *cmd, uint32_t timeout) {
    (void)queue; (void)timeout;
    if (!pending) return 0;
    *cmd = queued; pending = false; return pdPASS;
}
static void bsp_audio_set_volume(uint32_t percent) { (void)percent; }
static esp_err_t bsp_audio_sleep(void) { return sleep_result; }
static esp_err_t network_cancel(void) { ++network_cancels; return cancel_result; }
static int s_stream, s_net_events, s_net_requests=2;
static bool command_queue_fails;
static int stream_resets, stream_reset_result=pdPASS;
static uint32_t generation;
static net_request_t last_request;
#define NET_IDLE 1U
#define NET_FM_READY 2U
#define NET_FM_DECODER 4U
#define s_net net
static int xStreamBufferReset(int stream) { (void)stream; ++stream_resets; return stream_reset_result; }
static uint32_t generation_next(void) { return ++generation; }
static void xEventGroupClearBits(int event, unsigned bits) { (void)event; assert(bits==(NET_IDLE|NET_FM_READY|NET_FM_DECODER)); }
static void xEventGroupSetBits(int event, unsigned bits) { (void)event; assert(bits==NET_IDLE); }
static int xQueueSend(int queue, const void *data, int timeout) {
    assert(timeout==0);
    if (queue==s_commands) {
        if (command_queue_fails) return 0;
        assert(!pending); queued=*(const command_t *)data; pending=true; return pdPASS;
    }
    const net_request_t *request=data;
    assert(request->url && request->generation==net.generation);
    last_request=*request;
    return pdPASS;
}
#include "audio_network_start.h"
static esp_err_t network_start(const char *source) {
    assert(source && source[0]); ++network_starts;
    return start_result==ESP_OK ? actual_network_start(source) : start_result;
}
static esp_err_t network_start_at(const char *source, uint64_t offset, const char *validator, uint64_t total) {
    ++network_starts;
    return start_result==ESP_OK ? actual_network_start_at(source,offset,validator,total) : start_result;
}
static void esp_audio_simple_dec_close(void *handle) { assert(handle); ++decoder_closes; }
#include "audio_publish.h"
static void quiescent_set(bool value) { quiet = value; }
static void resource_log(const char *phase, uint32_t elapsed) { (void)phase; (void)elapsed; }
static void failure_log(const char *stage, int error) { (void)stage; (void)error; }
#include "audio_enqueue.h"

#define ESP_AUDIO_ERR_BUFF_NOT_ENOUGH (-8)
static int output_retry(uint32_t needed, uint32_t consumed, uint32_t decoded) {
    struct { uint32_t len, consumed; } raw={100,consumed};
    struct { uint32_t decoded_size, needed_size; } out={decoded,needed};
    int r=ESP_AUDIO_ERR_BUFF_NOT_ENOUGH;
    uint8_t *saved_input=input;
    size_t saved_offset=input_offset, saved_length=input_length;
#include "audio_pcm_retry.h"
    return 0; // Invalid/partial results bypass growth and enter the normal error guard.
decode_input:
    assert(input==saved_input && input_offset==saved_offset && input_length==saved_length);
    return 1;
failed:
#include "audio_worker_cleanup.h"
    return -1;
}

static void worker_ticks(int count) {
    command_t cmd;
    for (int tick = 0; tick < count; ++tick) {
#include "audio_worker_commands.h"
        ++decode_ticks; // Reached the real worker's active decode boundary.
    }
    return;
failed:
    assert(false && "Unexpected BSP failure in this command test");
}
static void send(command_type_t type, uint32_t value, const char *source) {
    assert(!pending);
    queued = (command_t){ .type = type, .value = value };
    if (source) {
        queued.url = malloc(strlen(source) + 1);
        assert(queued.url); strcpy(queued.url, source);
    }
    pending = true;
    worker_ticks(1);
    assert(!pending);
}

static void resume_probe(void) {
    for (int once=0;once<1;++once) {
#include "audio_resume_probe.h"
    }
}

#define pdMS_TO_TICKS(ms) (ms)
static size_t xStreamBufferBytesAvailable(int stream) { (void)stream; return 0; }
static size_t xStreamBufferReceive(int stream, void *buffer, size_t size, int ticks) {
    (void)stream; (void)buffer; (void)size; (void)ticks; return 0;
}
static int scan_tick(void) {
    for (int once=0;once<1;++once) {
#include "audio_mp3_scan.h"
    }
    return 0;
range_restart_zero:
    return -1;
}
static void scan_eof(void) {
#include "audio_mp3_scan_eof.h"
}

int main(void) {
    assert(STREAM_BYTES==12288 && STREAM_BYTES>=8192+NETWORK_BYTES);
    // Exercise actual bounded growth and allocation failure, retaining bytes.
    uint8_t *buffer=malloc(PCM_INITIAL_BYTES);
    assert(buffer); memset(buffer,0x5a,PCM_INITIAL_BYTES);
    size_t capacity=PCM_INITIAL_BYTES;
    uint8_t *original=buffer;
    assert(pcm_grow(&buffer,&capacity,PCM_INITIAL_BYTES)==ESP_ERR_INVALID_SIZE);
    assert(pcm_grow(&buffer,&capacity,PCM_BYTES+1)==ESP_ERR_INVALID_SIZE);
    assert(!realloc_calls && buffer==original && capacity==PCM_INITIAL_BYTES);
    realloc_fails=true;
    assert(pcm_grow(&buffer,&capacity,6144)==ESP_ERR_NO_MEM);
    assert(buffer==original && capacity==PCM_INITIAL_BYTES && buffer[4607]==0x5a);
    realloc_fails=false;
    assert(pcm_grow(&buffer,&capacity,6144)==ESP_OK && capacity==PCM_BYTES);
    assert(buffer[0]==0x5a && buffer[4607]==0x5a);
    assert(pcm_grow(&buffer,&capacity,PCM_BYTES)==ESP_ERR_INVALID_SIZE);
    free(buffer);
    // Unloaded seeks cannot start network I/O.
    send(CMD_SEEK, 30, NULL);
    assert(network_starts == 0 && !running);
    send(CMD_PLAY, 0, "https://example.test/episode.mp3");
    assert(stream_resets==1 && generation==1 && !net.done && net.error==ESP_OK);
    assert(s_status.state == PASSPORT_AUDIO_BUFFERING);
    assert(pcm_bytes==4608);
    // Actual retry branch preserves raw input; failed growth uses real cleanup.
    input_length=100; input_offset=7; memset(input,0x6b,100);
    assert(output_retry(6144,0,0)==1 && pcm_bytes==PCM_BYTES && input[7]==0x6b);
    send(CMD_PLAY,0,"https://example.test/episode.mp3");
    assert(pcm_bytes==PCM_INITIAL_BYTES);
    decoder=&s_lock; realloc_fails=true;
    assert(output_retry(6144,0,0)==-1);
    assert(!input && !pcm && !decoder && quiet && s_status.last_error==ESP_ERR_NO_MEM);
    realloc_fails=false;
    send(CMD_PLAY,0,"https://example.test/episode.mp3");
    decoder=&s_lock;
    assert(output_retry(PCM_BYTES+1,0,0)==-1 && !input && !pcm && !decoder);
    assert(s_status.last_error==ESP_ERR_INVALID_SIZE);
    send(CMD_PLAY,0,"https://example.test/episode.mp3");
    assert(output_retry(6144,101,0)==0 && pcm_bytes==PCM_INITIAL_BYTES);
    // Positive consumption on buffer shortage is ambiguous: never resend it.
    assert(output_retry(6144,10,0)==0 && pcm_bytes==PCM_INITIAL_BYTES);
    assert(output_retry(6144,0,2)==0 && pcm_bytes==PCM_INITIAL_BYTES);
    network_starts=1; network_cancels=0; decoder_closes=0;
    // Maximum stereo MP3 frame fits and downmixes all 1152 samples.
    for (unsigned i=0;i<2304;++i) ((int16_t *)pcm)[i]=100;
    decoded_frame(44100,2,1152);
    assert(pcm_samples==1152 && first_pcm==100);
    pcm_samples=0; format_calls=0; codec_awake=false; have_output=false; format_hz=0;
    decoded_samples=0;
    assert(running && !paused && network_starts == 1);
    decoder = &s_lock; // Simulate an already decoded stream.
    send(CMD_PAUSE, 1, NULL);
    assert(paused && s_status.state == PASSPORT_AUDIO_PAUSED);
    // A final released target commits one new stream, never audible decode.
    const int before_decode = decode_ticks, before_seek = network_starts;
    send(CMD_SEEK, 90, NULL);
    assert(network_starts == before_seek + 1 && decoder_closes == 1);
    assert(running && paused && s_status.state == PASSPORT_AUDIO_PAUSED);
    assert(s_status.position_seconds == 90 && target_seconds == 90);
    worker_ticks(3);
    assert(decode_ticks == before_decode && network_starts == before_seek + 1);
    // Volume and further target commits cannot change the pause intent.
    send(CMD_VOLUME, 61, NULL);
    send(CMD_SEEK, 45, NULL);
    assert(paused && s_status.volume == 61 && s_status.position_seconds == 45);
    send(CMD_PAUSE, 0, NULL);
    assert(!paused && s_status.state == PASSPORT_AUDIO_BUFFERING);
    worker_ticks(1); assert(decode_ticks == before_decode + 1);
    // Seeking an active stream stays active; it does not introduce a pause.
    send(CMD_SEEK, 120, NULL);
    assert(running && !paused && s_status.state == PASSPORT_AUDIO_BUFFERING);
    assert(s_status.position_seconds == 120);
    // A live sample timebase permits exact forward seek without any restart.
    decoder=&s_lock; have_output=true; format_hz=44100;
    decoded_samples=10*44100+22050; // Actual cursor is 10.5 s, not UI-rounded 10 s.
    input_length=211; input_offset=13; scanned=1234;
    s_status.sample_rate=44100; s_status.duration_seconds=3600;
    void *live_decoder=decoder; uint8_t *live_input=input, *live_pcm=pcm;
    const int live_starts=network_starts, live_cancels=network_cancels, live_closes=decoder_closes;
    send(CMD_PAUSE,1,NULL);
    send(CMD_SEEK,13,NULL);
    assert(running && paused && s_status.state==PASSPORT_AUDIO_PAUSED);
    assert(s_status.position_seconds==13 && discard_samples==110250);
    assert(decoder==live_decoder && input==live_input && pcm==live_pcm);
    assert(input_length==211 && input_offset==13 && scanned==1234 && !eos_flushed);
    assert(network_starts==live_starts && network_cancels==live_cancels && decoder_closes==live_closes);
    assert(s_status.sample_rate==44100 && s_status.duration_seconds==3600);
    worker_ticks(2); assert(decode_ticks==before_decode+1); // Paused seek cannot decode.
    // Retargeting below the old preview is still forward of the actual cursor.
    send(CMD_SEEK,11,NULL);
    assert(discard_samples==22050 && network_starts==live_starts);
    decoded_samples=11*44100-441;
    send(CMD_SEEK,11,NULL); // Exactly 441 mono samples remain before target.
    assert(discard_samples==441);
    send(CMD_PAUSE,0,NULL);
    for (int i=0;i<1024;++i) ((int16_t *)pcm)[i]=(int16_t)i;
    decoded_frame(44100,1,1024);
    assert(discard_samples==0 && decoded_samples==11*44100+583);
    assert(pcm_samples==583 && first_pcm==441 && format_calls==1);
    assert(s_status.state==PASSPORT_AUDIO_PLAYING);
    // An integer target below the fractional decoded cursor must restart.
    send(CMD_SEEK,11,NULL);
    assert(network_starts==live_starts+1 && network_cancels==live_cancels+1);
    assert(decoder_closes==live_closes+1 && !decoder && decoded_samples==0);
    // Successful HTTP EOF can still have FIFO/parser samples pending: keep them.
    decoder=&s_lock; have_output=true; format_hz=48000; decoded_samples=2*48000;
    net=(net_status_t){.done=true,.error=ESP_OK};
    const int eof_starts=network_starts;
    send(CMD_SEEK,2,NULL); // Exact equality needs no discard or restart.
    assert(!paused && discard_samples==0 && network_starts==eof_starts);
    send(CMD_SEEK,3,NULL);
    assert(network_starts==eof_starts && decoder==&s_lock && discard_samples==48000);
    send(CMD_SEEK,UINT32_MAX,NULL); // Widen before multiplication, never wrap.
    assert(discard_samples==(uint64_t)UINT32_MAX*48000-decoded_samples);
    send(CMD_SEEK,3,NULL);
    // Fully discarded frames leave the DAC asleep and yield after bounded work,
    // not after every frame. The first real 48k timebase remains confirmed.
    pcm_samples=0; codec_awake=false;
    clock_us=discard_yield_at-1000;
    const int before_yield=yield_calls, before_format=format_calls;
    decoded_frame(48000,1,100);
    assert(!pcm_samples && !codec_awake && format_calls==before_format && yield_calls==before_yield);
    clock_us=discard_yield_at;
    decoded_frame(48000,1,100);
    assert(yield_calls==before_yield+1 && discard_yield_at==clock_us+20000);
    assert(!pcm_samples && discard_samples==47800);
    // Sample-rate changes during discard are rejected, never mixed timebases.
    decoded_frame(44100,1,100);
    assert(!running && s_status.state==PASSPORT_AUDIO_ERROR);
    send(CMD_PLAY,0,"https://example.test/container.m4a");
    // Unknown first-PCM format cannot use byte/time estimates for any container.
    decoder=&s_lock; have_output=false; format_hz=0;
    const int uninitialized_starts=network_starts;
    send(CMD_SEEK,40,NULL);
    assert(network_starts==uninitialized_starts+1 && !decoder);
    // A terminal transport error or a finished session also takes safe restart.
    decoder=&s_lock; have_output=true; format_hz=48000; decoded_samples=48000;
    net=(net_status_t){.done=true,.error=ESP_FAIL};
    const int failed_transport_starts=network_starts;
    send(CMD_SEEK,50,NULL);
    assert(network_starts==failed_transport_starts+1 && !decoder);
    net=(net_status_t){0};
    running=false; decoder=&s_lock; have_output=true; format_hz=48000;
    const int finished_starts=network_starts;
    send(CMD_SEEK,60,NULL);
    assert(network_starts==finished_starts+1 && !decoder);
    // New PLAY deliberately clears the previous episode's paused intent.
    send(CMD_PAUSE, 1, NULL);
    send(CMD_PLAY, 5, "https://example.test/new.mp3");
    assert(!paused && running && target_seconds == 5);
    // A cancelled network must not be replaced, even for a paused seek.
    send(CMD_PAUSE, 1, NULL);
    cancel_result = ESP_FAIL;
    const int before_cancel_failure = network_starts;
    const int before_cancel_reset = stream_resets;
    char *retained_url = url;
    send(CMD_SEEK, 300, NULL);
    assert(network_starts == before_cancel_failure && url == retained_url);
    assert(stream_resets==before_cancel_reset);
    assert(!running && s_status.state == PASSPORT_AUDIO_ERROR);
    cancel_result = ESP_OK;
    // A new stream failure stays ERROR, not a spurious PAUSED success.
    start_result = ESP_ERR_NO_MEM;
    send(CMD_SEEK, 180, NULL);
    assert(!running && s_status.state == PASSPORT_AUDIO_ERROR && !input && !pcm);
    start_result = ESP_OK;
    // A failed FIFO reset cannot launch a replacement or report playback success.
    stream_reset_result=0;
    send(CMD_PLAY,0,"https://example.test/reset-error.mp3");
    assert(!running && !input && !pcm && s_status.last_error==ESP_ERR_INVALID_STATE);
    stream_reset_result=pdPASS;
    const int resets_before_stop=stream_resets;
    send(CMD_STOP, 0, NULL);
    assert(stream_resets==resets_before_stop);
    assert(!url && !input && !pcm && quiet && s_status.state == PASSPORT_AUDIO_IDLE);
    send(CMD_PLAY, 0, "https://example.test/final.mp3");
    assert(running && !paused);
    // Saved progress bootstraps metadata, then uses the real manual Range
    // dispatch before opening a decoder or discarding the entire saved prefix.
    send(CMD_PLAY,3000,"https://example.test/resume.mp3");
    assert(resume_probe_pending && !resume_seek_ready && !decoder);
    mp3_seek=(passport_mp3_seek_t){.valid=true,.cbr=true,.frames=137813,.audio_bytes=57600000,
        .audio_offset=120,.sample_rate=44100,.frame_samples=1152};
    net.total_bytes=57600120; strcpy(net.validator,"\"fixture\"");
    resume_probe();
    assert(!resume_probe_pending && resume_seek_ready);
    const int resume_starts=network_starts;
    worker_ticks(1);
    assert(range_mode && network_starts==resume_starts+1 && !resume_seek_ready && !resume_probe_pending);
    assert(last_request.offset>47000000 && discard_samples==44100 && position_base_samples==2999ULL*44100);
    assert(!decoder && !have_output && s_status.position_seconds==3000);
    // An RSS/size plan must reuse precisely the same automatic Range dispatch.
    send(CMD_PLAY,1236,"https://example.test/old-no-index.mp3");
    unsigned char proportional_frame[417]={255,251,144};
    assert(passport_mp3_seek_proportional(&mp3_seek,proportional_frame,
                                        sizeof(proportional_frame),5645,83828613,5238));
    net.total_bytes=83828613; strcpy(net.validator,"\"fixture\"");
    resume_probe(); assert(resume_seek_ready);
    worker_ticks(1);
    assert(range_mode && s_status.position_approximate && s_status.duration_seconds==5238);
    assert(last_request.offset>19700000 && last_request.offset<19800000);
    assert(discard_samples==44100 && position_base_samples==1235ULL*44100);
    // A pause arriving between metadata and the automatic seek stays silent.
    send(CMD_PLAY,1000,"https://example.test/paused-resume.mp3");
    mp3_seek=(passport_mp3_seek_t){.valid=true,.cbr=true,.frames=137813,.audio_bytes=57600000,
        .audio_offset=120,.sample_rate=44100,.frame_samples=1152};
    net.total_bytes=57600120; strcpy(net.validator,"\"fixture\"");
    resume_probe(); send(CMD_PAUSE,1,NULL);
    assert(paused && resume_seek_ready);
    worker_ticks(1);
    assert(paused && range_mode && !resume_seek_ready && s_status.state==PASSPORT_AUDIO_PAUSED);
    // Lack of a strong resource validator preserves the exact fallback and
    // cannot repeatedly restart/probe while sequential PCM catches up.
    send(CMD_PLAY,3000,"https://example.test/no-validator.mp3");
    mp3_seek=(passport_mp3_seek_t){.valid=true,.cbr=true,.frames=137813,.audio_bytes=57600000,
        .audio_offset=120,.sample_rate=44100,.frame_samples=1152};
    net.total_bytes=57600120;
    resume_probe();
    assert(!resume_probe_pending && !resume_seek_ready && !range_mode);
    resume_probe_pending=true; mp3_seek.valid=false; strcpy(net.validator,"\"fixture\"");
    resume_probe();
    assert(!resume_probe_pending && !resume_seek_ready);
    // An accepted newer PLAY takes priority over a prepared automatic seek.
    mp3_seek.valid=true; resume_probe_pending=true; resume_probe();
    assert(resume_seek_ready);
    send(CMD_PLAY,0,"https://example.test/replacement.mp3");
    assert(!resume_seek_ready && !resume_probe_pending && !range_mode && target_seconds==0);
    // Exercise the real encoded-scan transition, not a duplicated PCM/time
    // calculation: mixed bitrates consume frames until one-second preroll.
    mp3_scanning=true; target_seconds=3; mp3_scan=(passport_mp3_scan_t){0};
    mp3_scan_bytes=0; mp3_seek.valid=false; mp3_seek.sample_rate=0;
    for (unsigned n=0;n<77;++n) {
        memset(input,0,INPUT_BYTES); input[0]=255; input[1]=251;
        input[2]=n%2 ? 0xb0 : 0x90;
        passport_mp3_frame_t f; assert(passport_mp3_header(input,&f));
        input_length=f.bytes; input_offset=0;
        assert(scan_tick()==0 && input_offset==f.bytes && mp3_scanning && !range_mode);
    }
    input_offset=0;
    assert(scan_tick()==0 && !mp3_scanning && range_mode && range_synced);
    assert(position_base_samples==77U*1152U && discard_samples==3U*44100-position_base_samples);
    assert(mp3_seek.sample_rate==44100 && !mp3_seek.valid && !s_status.position_approximate);
    // Targets past the actual EOF finish at counted audio time. A final ID3v1
    // tag is consumed without interpreting it as a corrupt MPEG frame.
    mp3_scanning=true; range_mode=false; net.done=true;
    memset(input,0,128); memcpy(input,"TAG",3); input_length=128; input_offset=0;
    assert(scan_tick()==0 && input_offset==128 && mp3_scanning);
    have_output=false; decoded_samples=0; scan_eof();
    assert(!mp3_scanning && have_output && position_base_samples==77U*1152U);
    assert(s_status.duration_seconds==2 && s_status.sample_rate==44100);
    // STOP cancels compressed scanning before another frame can be consumed.
    mp3_scanning=true;
    send(CMD_STOP,0,NULL);
    assert(!mp3_scanning && !running && !input && !pcm);
    send(CMD_PLAY,0,"https://example.test/final.mp3");
    // Known Info + authenticated whole resource selects bounded Range even
    // for a 50-minute jump; sample cursor becomes local to the Range origin.
    mp3_seek=(passport_mp3_seek_t){.valid=true,.cbr=true,.frames=137813,.audio_bytes=57600000,
        .audio_offset=120,.sample_rate=44100,.frame_samples=1152};
    net.total_bytes=57600120; strcpy(net.validator,"\"fixture\"");
    decoder=&s_lock; have_output=true; format_hz=44100; decoded_samples=10*44100;
    const int fast_starts=network_starts;
    send(CMD_SEEK,3000,NULL);
    assert(range_mode && !range_synced && network_starts==fast_starts+1);
    assert(last_request.offset>47000000 && last_request.total_bytes==57600120 && !strcmp(last_request.validator,"\"fixture\""));
    assert(position_base_samples==2999ULL*44100 && discard_samples==44100 && decoded_samples==0);
    assert(s_status.position_approximate && s_status.duration_seconds==3600);
    // Paused fast seek retains silence and copied validator, never stack data.
    net.total_bytes=57600120; strcpy(net.validator,"\"fixture\"");
    send(CMD_PAUSE,1,NULL); send(CMD_SEEK,1000,NULL);
    assert(paused && range_mode && s_status.state==PASSPORT_AUDIO_PAUSED);
    assert(position_base_samples==999ULL*44100 && last_request.offset>15000000);
    // Old PCM cannot mark a more recently accepted seek complete.
    s_seek_active_id=4; s_status.seek_request_id=5; s_status.seeking=true;
    publish(PASSPORT_AUDIO_PLAYING,ESP_OK); assert(s_status.seeking);
    s_seek_active_id=5; publish(PASSPORT_AUDIO_PLAYING,ESP_OK);
    assert(!s_status.seeking && s_status.seek_complete_id==5);
    // Actual producer marks pending before the worker can publish old PCM,
    // and a rejected newer request restores an already pending predecessor.
    assert(enqueue((command_t){.type=CMD_SEEK,.value=500})==ESP_OK);
    assert(s_status.seeking && s_status.seek_request_id==6 && pending);
    command_queue_fails=true;
    assert(enqueue((command_t){.type=CMD_SEEK,.value=600})==ESP_ERR_TIMEOUT);
    assert(s_status.seeking && s_status.seek_request_id==6);
    command_queue_fails=false; worker_ticks(1);
    assert(s_seek_active_id==6 && paused);
    send(CMD_STOP, 0, NULL);
    assert(network_cancels > 0 && !url && !input && !pcm);
    // Station commands retain a dedicated transport and cannot reuse podcast
    // bytes on resume. Pause remains silent; resume opens a fresh station URL.
    queued=(command_t){.type=CMD_PLAY,.fm=true,.url=strdup("https://example.test/station")};
    pending=true;worker_ticks(1);
    assert(playing_fm && s_network_fm && s_status.fm && !s_status.seek_supported);
    send(CMD_PAUSE,1,NULL);assert(paused);
    int station_starts=network_starts;
    send(CMD_PAUSE,0,NULL);
    assert(!paused && playing_fm && network_starts==station_starts+1 && !strcmp(url,"https://www.gcores.com/radios#fm"));
    send(CMD_STOP,0,NULL);
    send(CMD_PLAY,0,"https://example.test/podcast.mp3");
    assert(!playing_fm && !s_network_fm && !s_status.fm && s_status.seek_supported);
    send(CMD_STOP,0,NULL);
    puts("Passport audio actual dispatch/PCM: PASS (continuous forward, sample boundary, pause, EOF/fallback, bounded yield)");
}
