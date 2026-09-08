#pragma once
#include "FreeRTOS.h"
inline TickType_t xTaskGetTickCount() { return fakeNow; }
inline void vTaskDelayUntil(TickType_t*,TickType_t) {}
inline int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*) { return pdPASS; }
