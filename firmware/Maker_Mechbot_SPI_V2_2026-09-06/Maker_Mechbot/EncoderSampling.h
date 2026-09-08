#pragma once

#include <Arduino.h>
#include <soc/soc.h>
#include <soc/gpio_reg.h>

// Sample the state of an encoder pair (pinA, pinB) in a single register read.
// Both pins must belong to the same GPIO input bank (either 0-31 or 32-39).
// Returns a 2‑bit value: (A << 1) | B.
static IRAM_ATTR inline uint8_t sampleEncoderState(uint8_t pinA, uint8_t pinB) {
    // Determine which bank the pins are in.
    bool highBank = pinA >= 32 || pinB >= 32;
    uint32_t gpioIn;
    if (highBank) {
        // Pins 32-39 are in the high bank.
        gpioIn = REG_READ(GPIO_IN1_REG);
    } else {
        // Pins 0-31 are in the low bank.
        gpioIn = REG_READ(GPIO_IN_REG);
    }
    // Shift pins into position 0-31 for extraction.
    uint8_t shiftA = highBank ? pinA - 32 : pinA;
    uint8_t shiftB = highBank ? pinB - 32 : pinB;
    uint8_t A = (gpioIn >> shiftA) & 0x01;
    uint8_t B = (gpioIn >> shiftB) & 0x01;
    return (A << 1) | B;
}
