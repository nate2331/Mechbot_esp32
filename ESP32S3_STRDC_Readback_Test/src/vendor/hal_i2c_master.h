/*
 * Teensyduino I2C Master HAL H File.
 *
 * @file        hal_i2c_master.h
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

#ifndef HAL_I2C_MASTER_H
#define HAL_I2C_MASTER_H

#include <Arduino.h>
#include <Wire.h>

#define I2C_MAX_BUFFER_SIZE 32 // 32 Bytes

typedef struct {

    TwoWire *bus;
    uint8_t curr_addr;
    uint32_t speed;

} i2c_handle_t; // I2C Peripheral Handler

void hal_i2c_init(i2c_handle_t *, uint32_t);
void hal_i2c_start(i2c_handle_t *, uint8_t);
uint8_t hal_i2c_stop(i2c_handle_t *, bool);
uint8_t hal_i2c_read(i2c_handle_t *, uint8_t *, uint16_t);
void hal_i2c_write(i2c_handle_t *, uint8_t *, uint8_t);
void hal_i2c_close(i2c_handle_t *);

// The ESP32-S3 has two real I2C controllers.
extern i2c_handle_t i2c1;
extern i2c_handle_t i2c2;

#endif
