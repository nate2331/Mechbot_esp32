#pragma once
#include "FreeRTOS.h"
using SemaphoreHandle_t = void*;
inline int fakeMutexStorage = 0;
inline bool fakeMutexOk = true;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return fakeMutexOk ? &fakeMutexStorage : nullptr; }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return pdPASS; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return pdPASS; }
