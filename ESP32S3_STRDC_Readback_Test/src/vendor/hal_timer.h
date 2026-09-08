/*
 * Teensyduino Timer HAL H File.
 *
 * @file        hal_timer.h
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

#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include <Arduino.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {

    uint32_t start;
    uint32_t curr;
    uint8_t lapsed;
    uint8_t expLapsed;
    uint32_t exp;
    uint32_t set;
    uint8_t isExp;

} timer_handle_t; // Handler for Timer


void hal_timer_init(timer_handle_t *, uint32_t);
void hal_timer_start(timer_handle_t *);
void hal_timer_reset(timer_handle_t *);
uint8_t hal_timer_check_exp(timer_handle_t *);
void hal_timer_blocking_delay(uint32_t);

#ifdef __cplusplus
}
#endif

#endif