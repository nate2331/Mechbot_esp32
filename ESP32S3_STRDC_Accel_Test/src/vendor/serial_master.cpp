/*
 * Serial Master C++ File.
 *
 * @file        serial_master.cpp
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

#include "serial_master.h"
#include "timer.h"


/****************************************************************************
 * @brief Initialize and open a serial (UART) peripheral.
 * @param handle Pointer to serial handler. Handler should have configuration defined before calling this function.
 * @param speed Speed (baudrate) of operation of peripheral. Defaults to 115200.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t serial_open(serial_handle_t *handle, uint32_t speed = 115200, uint8_t busType = UART_TYPE_BASIC)
{
    
    return hal_serial_open(handle, speed, busType);

}

/****************************************************************************
 * @brief Read serial data from peripheral. Will increment buffer.
 * @param handle Pointer to serial handler.
 * @param data Data to receive over serial, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 65535 bytes (uint16_t).
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t serial_read(serial_handle_t *handle, uint8_t *data, uint16_t bytes)
{

    timer_handle_t to_timer; // Create timer for timeout
    timer_init(&to_timer, 5000); // 5ms

    for (uint16_t i = 0; i < bytes; i++)
    {
        timer_reset(&to_timer); // Restart timer
        while (hal_serial_read_available(handle) == 0) // Wait for data to be available
        {
            if (timer_check_exp(&to_timer))
                return 1;

        }
        data[i] = hal_serial_read(handle);
    }

    return 0;

}

/****************************************************************************
 * @brief Return number of bytes in serial read buffer
 * @param handle Pointer to serial handler.
 * @return Number of bytes available to read
 ****************************************************************************/
uint16_t serial_read_available(serial_handle_t *handle)
{

    return hal_serial_read_available(handle);

}

/****************************************************************************
 * @brief Read next byte in buffer without removing it.
 * @param handle Pointer to serial handler.
 * @return Next data byte in buffer for read. Returns -1 if no data.
 ****************************************************************************/
uint8_t serial_peek(serial_handle_t *handle)
{
    return hal_serial_peek(handle);
}

/****************************************************************************
 * @brief Write serial data on peripheral.
 * @param data Data to send over serial, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 65535 bytes (uint16_t).
 ****************************************************************************/
void serial_write(serial_handle_t *handle, uint8_t *data, uint16_t bytes)
{
    
    uint16_t avail;

    if (hal_serial_write_available(handle) < bytes) // Check if there's space in buffer for entire message
        hal_serial_flush(handle); // Wait for remaining bytes in buffer to send

    avail = hal_serial_write_available(handle); // Get buffer space available for immediate transmission

    for (uint16_t i = 0; i < bytes; i++)
    {
        hal_serial_write(handle, data[i]);

        if ((i % avail == 0) && (i > 0)) // Ensure we don't overload buffer
            hal_serial_flush(handle); // Wait for buffer to clear
    }

}

/****************************************************************************
 * @brief Add additional memory to serial read buffer
 * @param buffer Pointer to array of bytes for additional buffer location.
 * @param bytes Length of additional buffer array (buffer).
 ****************************************************************************/
void serial_buffer_read_add(serial_handle_t *handle, uint8_t *buffer, size_t length)
{
    
    hal_serial_buffer_read_add(handle, buffer, length);

}

/****************************************************************************
 * @brief Add additional memory to serial write buffer
 * @param buffer Pointer to array of bytes for additional buffer location.
 * @param bytes Length of additional buffer array (buffer).
 ****************************************************************************/
void serial_buffer_write_add(serial_handle_t *handle, uint8_t *buffer, size_t length)
{
    
    hal_serial_buffer_write_add(handle, buffer, length);

}

/****************************************************************************
 * @brief Clear buffer, losing all unread data.
 * @param handle Pointer to serial handler.
 ****************************************************************************/
void serial_buffer_clear(serial_handle_t *handle)
{
    hal_serial_clear(handle);
}

/****************************************************************************
 * @brief Close serial peripheral.
 * @param handle Pointer to serial handler.
 ****************************************************************************/
void serial_close(serial_handle_t *handle)
{
    hal_serial_close(handle);
}
