#pragma once
#include "FreeRTOS.h"
using SemaphoreHandle_t=int*;
inline int fakeMutex=0;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return &fakeMutex; }
inline void xSemaphoreTake(SemaphoreHandle_t,unsigned) {}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
