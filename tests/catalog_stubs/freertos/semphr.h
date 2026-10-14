#pragma once
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void *>(1); }
static inline int xSemaphoreTake(SemaphoreHandle_t,unsigned) { return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
