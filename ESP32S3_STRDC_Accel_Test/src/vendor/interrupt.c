/*
 * Interrupt Library C File.
 *
 * @file        interrupt.c
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

#include "interrupt.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * @brief Create interrupt handler for a GPIO interrupt.
 * @param pin Pin number as listed in the Teensy Information.
 * @param mode Mode to configure interrupt. See GPIO library.
 * @param callback Callback function for interrupt.
 * @return Pointer to interrupt handler.
 ****************************************************************************/
isr_handle_t* interrupt_init(uint8_t pin, uint8_t mode, void (*callback)(void))
{

    isr_handle_t *newInt = (isr_handle_t *)malloc(sizeof(isr_handle_t));
    if (newInt == NULL) // Check for success
        return NULL;

    newInt->pin = pin;
    newInt->mode = mode;
    newInt->callback = callback;

    hal_interrupt_init(newInt);

    return newInt;

}

/****************************************************************************
 * @brief Disable an initialized and previously set interrupt.
 * @param handle Pointer to interrupt handler. Handler should be initialized and previously set.
 ****************************************************************************/
void interrupt_disable(isr_handle_t * handle)
{
    hal_interrupt_disable(handle);
}

/****************************************************************************
 * @brief Enable an initialized and previously set interrupt.
 * @param handle Pointer to interrupt handler. Handler should be initialized and previously set.
 ****************************************************************************/
void interrupt_enable(isr_handle_t * handle)
{
    hal_interrupt_enable(handle);
}

/****************************************************************************
 * @brief Blocks all regular interrupts, leaving only NMI and HardFault.
 ****************************************************************************/
void interrupt_disable_all(void)
{
    hal_interrupt_disable_all();
}

/****************************************************************************
 * @brief Allows all regular interrupts to function again.
 ****************************************************************************/
void interrupt_enable_all(void)
{
    hal_interrupt_enable_all();
}

/****************************************************************************
 * @brief Register and enable a previously initialized interrupt.
 * @param handle Pointer to interrupt handler. Handler should be initialized.
 ****************************************************************************/
void interrupt_set(isr_handle_t * handle)
{
    hal_interrupt_set(handle);
}

/****************************************************************************
 * @brief Update the priority of a previously initialized interrupt.
 * @param handle Pointer to interrupt handler. Handler should be initialized.
 * @param priority Interrupt priority 0-255.
 ****************************************************************************/
void interrupt_priority(isr_handle_t * handle, uint8_t priority)
{
    hal_interrupt_priority(handle, priority);
}

/****************************************************************************
 * @brief Disable and free interrupt. Calling this function will render the interrupt handler useless.
 * @param handle Pointer to interrupt handler. Handler should be initialized and previously set.
 ****************************************************************************/
void interrupt_clear(isr_handle_t * handle)
{
    hal_interrupt_clear(handle);

    free(handle); // Free previously allocated memory
}

#ifdef __cplusplus
}
#endif