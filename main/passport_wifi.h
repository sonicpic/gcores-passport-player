#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t passport_wifi_init(void);
bool passport_wifi_connected(void);
bool passport_wifi_provisioning(void);
const char *passport_wifi_ap_password(void);
/* Explicit manual entry (or init with no saved credentials). Blocking worker
 * API; never call from an HTTP/event handler or LVGL/button callback. */
esp_err_t passport_wifi_start_provisioning(void);
/* Call from the application worker, never an HTTP/event handler or LVGL/button
 * callback. GOT_IP queues AP shutdown; poll drains HTTP before disabling AP. */
void passport_wifi_poll(void);
/* Synchronously close only the provisioning HTTP server/AP. Existing STA
 * connection, credentials and connection retries are preserved. Idempotent.
 * Same worker-context restriction as poll; return errors for UI handling. */
esp_err_t passport_wifi_end_provisioning(void);
esp_err_t passport_wifi_stop(void);
