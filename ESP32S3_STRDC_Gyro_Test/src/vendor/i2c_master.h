/*
 * I2C Master HAL H File.
 *
 * @file        i2c_master.h
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

#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include "hal_i2c_master.h"

void i2c_open(i2c_handle_t *, uint32_t);
uint8_t i2c_find(i2c_handle_t *, uint8_t);
void i2c_set_addr(i2c_handle_t *, uint8_t);
uint8_t i2c_read(i2c_handle_t *, uint8_t *, uint16_t);
uint8_t i2c_write(i2c_handle_t *, uint8_t *, uint8_t);
uint8_t i2c_read_reg(i2c_handle_t *, uint8_t *, uint8_t, uint8_t *, uint16_t);
void i2c_close(i2c_handle_t *);

#endif