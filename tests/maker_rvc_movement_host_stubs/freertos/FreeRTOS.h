#pragma once
#include "Arduino.h"
using TickType_t = uint32_t;
using TaskHandle_t = void*;
constexpr int pdPASS = 1;
constexpr unsigned portMAX_DELAY = UINT32_MAX;
inline TickType_t pdMS_TO_TICKS(unsigned ms) { return ms; }
