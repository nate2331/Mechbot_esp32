#pragma once
#include "Arduino.h"
using TickType_t=uint32_t;
constexpr unsigned portMAX_DELAY=0xFFFFFFFF;
constexpr int pdPASS=1;
inline TickType_t pdMS_TO_TICKS(unsigned ms) { return ms; }
