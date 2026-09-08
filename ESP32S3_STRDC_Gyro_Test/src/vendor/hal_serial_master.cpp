/*
 * Teensyduino Serial Master HAL C++ File.
 *
 * @file        hal_serial_master.cpp
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

#include "hal_serial_master.h"

// Actual ESP32-S3 UART peripherals; Serial0 remains the sketch console.
serial_handle_t uart1 = {.bus = &Serial1, .format = SERIAL_8N1, .rxPin = 255, .txPin = 255};
serial_handle_t uart2 = {.bus = &Serial2, .format = SERIAL_8N1, .rxPin = 255, .txPin = 255};

// This test ports the basic UART API for link completeness. Flow control and
// RS485 require a separate hardware-specific configuration and return failure.
uint8_t hal_serial_open(serial_handle_t *handle, uint32_t speed, uint8_t busType)
{
    if (handle == nullptr || handle->bus == nullptr || busType != UART_TYPE_BASIC)
        return 1;

    handle->type = busType;
    const int8_t rx = handle->rxPin == 255 ? -1 : (int8_t)handle->rxPin;
    const int8_t tx = handle->txPin == 255 ? -1 : (int8_t)handle->txPin;
    handle->bus->begin(speed, handle->format, rx, tx);
    if (!*(handle->bus))
        delay(250);
    return *(handle->bus) ? 0 : 1;
}

uint8_t hal_serial_read(serial_handle_t *handle)
{
    return handle->bus->read();
}

void hal_serial_write(serial_handle_t *handle, uint8_t data)
{
    handle->bus->write(data);
}

uint16_t hal_serial_read_available(serial_handle_t *handle)
{
    return handle->bus->available();
}

uint8_t hal_serial_peek(serial_handle_t *handle)
{
    return handle->bus->peek();
}

void hal_serial_buffer_read_add(serial_handle_t *handle, uint8_t *buffer, size_t length)
{
    (void)handle;
    (void)buffer;
    (void)length;
    // ESP32 owns its buffers; it cannot append caller-owned memory as Teensy does.
    // Configure HardwareSerial::setRxBufferSize() before begin() when needed.
    log_e("STRDC ESP32 HAL: external UART RX buffer append is unsupported");
}

void hal_serial_buffer_write_add(serial_handle_t *handle, uint8_t *buffer, size_t length)
{
    (void)handle;
    (void)buffer;
    (void)length;
    // Configure HardwareSerial::setTxBufferSize() before begin() when needed.
    log_e("STRDC ESP32 HAL: external UART TX buffer append is unsupported");
}

void hal_serial_clear(serial_handle_t *handle)
{
    // Preserve clear() as RX discard, unlike ESP32 flush() which waits for TX.
    while (handle->bus->available() > 0)
        handle->bus->read();
}

size_t hal_serial_write_available(serial_handle_t *handle)
{
    return handle->bus->availableForWrite();
}

void hal_serial_close(serial_handle_t *handle)
{
    handle->bus->end();
}

void hal_serial_flush(serial_handle_t *handle)
{
    handle->bus->flush();
}