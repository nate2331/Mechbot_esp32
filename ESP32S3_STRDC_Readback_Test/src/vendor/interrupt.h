/*
 * Interrupt Library H File.
 *
 * @file        interrupt.h
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

#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "hal_interrupt.h"


#ifdef __cplusplus
extern "C" {
#endif

isr_handle_t* interrupt_init(uint8_t, uint8_t, void (*)(void));
void interrupt_disable(isr_handle_t *);
void interrupt_enable(isr_handle_t *);
void interrupt_disable_all(void);
void interrupt_enable_all(void);
void interrupt_set(isr_handle_t *);
void interrupt_priority(isr_handle_t *, uint8_t);
void interrupt_clear(isr_handle_t *);

#ifdef __cplusplus
}
#endif

#endif