#pragma once
#include <stdio.h>
#define ESP_LOGW(tag, ...) do { fprintf(stderr, "%s: ", tag); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define ESP_LOGE ESP_LOGW
