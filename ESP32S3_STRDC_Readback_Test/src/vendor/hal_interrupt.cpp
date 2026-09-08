/*
 * Teensyduino Interrupt HAL C File.
 *
 * @file        hal_interrupt.c
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

 /* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2018 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "hal_interrupt.h"
#include "hal_gpio.h"
#include <driver/gpio.h>

// ESP32 modes differ from STRDC modes: GPIO_LOW (0) must become ONLOW (4).
static int esp32_interrupt_mode(uint8_t mode)
{
    switch (mode)
    {
        case GPIO_LOW: return ONLOW;
        case GPIO_HIGH: return ONHIGH;
        case GPIO_FALLING: return FALLING;
        case GPIO_RISING: return RISING;
        case GPIO_CHANGE: return CHANGE;
        default: return DISABLED;
    }
}

void hal_interrupt_init(isr_handle_t *handle)
{
    // The middleware has already initialized pin, mode, and callback.
    // ESP32 needs no Teensy register/mask discovery before attachInterrupt().
    (void)handle;
}

void ARDUINO_ISR_ATTR hal_interrupt_disable(isr_handle_t *handle)
{
    if (handle != nullptr && GPIO_IS_VALID_GPIO(handle->pin))
        gpio_intr_disable((gpio_num_t)handle->pin);
}

void ARDUINO_ISR_ATTR hal_interrupt_enable(isr_handle_t *handle)
{
    if (handle != nullptr && GPIO_IS_VALID_GPIO(handle->pin))
        gpio_intr_enable((gpio_num_t)handle->pin);
}

void hal_interrupt_disable_all(void)
{
    noInterrupts();
}

void hal_interrupt_enable_all(void)
{
    interrupts();
}

void hal_interrupt_set(isr_handle_t *handle)
{
    if (handle != nullptr && GPIO_IS_VALID_GPIO(handle->pin))
        attachInterrupt(handle->pin, handle->callback, esp32_interrupt_mode(handle->mode));
}

void hal_interrupt_priority(isr_handle_t *handle, uint8_t priority)
{
    (void)handle;
    (void)priority;
    // ESP32 Arduino shares a GPIO interrupt service: per-pin priority is unavailable.
    log_e("STRDC ESP32 HAL: per-pin interrupt priority is unsupported");
}

void hal_interrupt_clear(isr_handle_t *handle)
{
    if (handle != nullptr && GPIO_IS_VALID_GPIO(handle->pin))
        detachInterrupt(handle->pin);
}