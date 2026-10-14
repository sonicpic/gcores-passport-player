#pragma once
#include "esp_err.h"
#define HTTP_EVENT_ON_HEADER 1
struct esp_http_client_event_t { void *user_data; int event_id; const char *header_key; const char *header_value; };
struct esp_http_client_config_t {
 const char *url; int timeout_ms,buffer_size,buffer_size_tx,max_redirection_count;
 bool disable_auto_redirect; const char *user_agent;
 int (*crt_bundle_attach)(void *);
 esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data;
};
struct StubHttpClient;
typedef StubHttpClient *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t,const char *);
