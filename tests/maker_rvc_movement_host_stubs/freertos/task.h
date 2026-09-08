#pragma once
#include "FreeRTOS.h"
inline void (*fakeMotorTask)(void*) = nullptr;
inline unsigned fakeTaskCreateCalls = 0;
inline int fakeTaskCreateResult = pdPASS;
inline TickType_t xTaskGetTickCount() { return fakeNow; }
inline void vTaskDelayUntil(TickType_t*, TickType_t) {}
inline void vTaskDelay(TickType_t) {}
inline int xTaskCreate(void (*fn)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*) {
  ++fakeTaskCreateCalls; fakeMotorTask = fn; return fakeTaskCreateResult;
}
inline int xTaskCreatePinnedToCore(void (*fn)(void*), const char* name, unsigned size,
                                 void* arg, unsigned priority, TaskHandle_t* handle, int) {
  return xTaskCreate(fn, name, size, arg, priority, handle);
}
