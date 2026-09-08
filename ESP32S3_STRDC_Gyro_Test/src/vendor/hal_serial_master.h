/*
 * Teensyduino Serial Master HAL H File.
 *
 * @file        hal_serial_master.h
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

#ifndef HAL_SERIAL_MASTER_H
#define HAL_SERIAL_MASTER_H

#include <Arduino.h>

// UART Setup Type
#define UART_TYPE_BASIC 0
#define UART_TYPE_FLOW_RTS 1
#define UART_TYPE_FLOW_CTS 2
#define UART_TYPE_RS485 4

typedef struct {

    HardwareSerial *bus;
    uint32_t format; // ESP32 SERIAL_* format constants require 32 bits
    uint8_t type; // See flow controls
    uint8_t rxPin;
    uint8_t txPin;
    uint8_t ctsPin;
    uint8_t rtsPin;
    uint8_t rs4Pin; // RS4 enable pin

} serial_handle_t; // Handler for Serial Peripheral

uint8_t hal_serial_open(serial_handle_t *, uint32_t, uint8_t);
uint8_t hal_serial_read(serial_handle_t *);
void hal_serial_write(serial_handle_t *, uint8_t);
uint16_t hal_serial_read_available(serial_handle_t *);
uint8_t hal_serial_peek(serial_handle_t *);
void hal_serial_buffer_read_add(serial_handle_t *, uint8_t *, size_t);
void hal_serial_buffer_write_add(serial_handle_t *, uint8_t *, size_t);
void hal_serial_clear(serial_handle_t *);
size_t hal_serial_write_available(serial_handle_t *);
void hal_serial_close(serial_handle_t *);
void hal_serial_flush(serial_handle_t *);

// Existing Peripherals
extern serial_handle_t uart1;
extern serial_handle_t uart2;

#endif