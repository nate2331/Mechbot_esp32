#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cassert>
#include "gpio_reg.h"

inline unsigned gpioReadCount[2] = {};

inline uint32_t fakeReadGpioRegister(uint32_t reg) {
    assert(reg == GPIO_IN_REG || reg == GPIO_IN1_REG);
    gpioReadCount[reg]++;
    uint32_t snapshot = 0;
    if (reg == GPIO_IN_REG) {
        for (int i = 0; i < 32; ++i) {
            snapshot |= (static_cast<uint32_t>(pinInput[i] & 1) << i);
        }
    } else {
        for (int i = 0; i < 32; ++i) {
            snapshot |= (static_cast<uint32_t>(pinInput[32 + i] & 1) << i);
        }
    }
    return snapshot;
}

#define REG_READ(reg) fakeReadGpioRegister(reg)
