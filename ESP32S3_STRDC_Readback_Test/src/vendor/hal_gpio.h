/*
 * Teensyduino GPIO HAL H File.
 *
 * @file        hal_gpio.h
 * @author      Alex Zundel
 * @copyright   Copyright (c) 2025 Stardust Orbital
 *
 * MIT License
 * 
 * Copyright (c) 2025 Stardust Orbital
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <Arduino.h>


#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_LOW 0
#define GPIO_HIGH 1
#define GPIO_FALLING 2
#define GPIO_RISING 3
#define GPIO_CHANGE 4

#define GPIO_MODE_OUTPUT OUTPUT
#define GPIO_MODE_INPUT INPUT
#define GPIO_MODE_INPUT_PULLUP INPUT_PULLUP
#define GPIO_MODE_INPUT_PULLDOWN INPUT_PULLDOWN
#define GPIO_MODE_OUTPUT_OPENDRAIN OUTPUT_OPEN_DRAIN
// INPUT_DISABLE is not provided by the ESP32 Arduino GPIO API.

void hal_gpio_mode(uint8_t, uint8_t);
void hal_gpio_write(uint8_t, uint8_t);
uint8_t hal_gpio_read(uint8_t);
void hal_gpio_toggle(uint8_t);

#ifdef __cplusplus
}
#endif

#endif
