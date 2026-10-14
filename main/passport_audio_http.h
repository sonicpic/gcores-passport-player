/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Initialize to zero for each HTTP response, including redirect hops. No heap
 * allocation or I/O. Only a quoted strong ETag is an accepted range identity. */
typedef struct {
    uint64_t range_start, range_end, range_total;
    bool range_seen, range_valid, invalid;
    bool etag_seen, etag_strong, etag_invalid, encoded;
    char validator[128];
} passport_http_response_t;

typedef enum {
    PASSPORT_HTTP_ACCEPT,
    PASSPORT_HTTP_RESTART_ZERO,
    PASSPORT_HTTP_REJECT
} passport_http_decision_t;

void passport_http_header(passport_http_response_t *response, const char *key, const char *value);
bool passport_http_strong_etag(const char *value);
/* offset zero requires 200; unknown whole-resource length is represented as 0.
 * Nonzero offset requires a complete open-ended 206 range, matching known total
 * and strong ETag. Content-Length -1 means unknown (chunked): the network worker
 * must still check actual bytes delivered against the accepted range at EOF.
 * Failed validation leaves full_total untouched. RESTART_ZERO requires a new
 * generation and reset decoder/FIFO; never append a new 200 body to old PCM. */
passport_http_decision_t passport_http_validate(const passport_http_response_t *response,
    int status, int64_t content_length, uint64_t offset, uint64_t expected_total,
    const char *expected_validator, uint64_t *full_total);
