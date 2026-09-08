/*
 * Teensyduino SPI Master HAL H File.
 *
 * @file        hal_spi_master.h
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

#ifndef HAL_SPI_MASTER_H
#define HAL_SPI_MASTER_H

#include <Arduino.h>
#include <SPI.h>

#define SPI_BIT_ORDER_LSB SPI_LSBFIRST
#define SPI_BIT_ORDER_MSB SPI_MSBFIRST

#define SPI_MODE_0 SPI_MODE0 // CPOL: 0, CPHA: 0
#define SPI_MODE_1 SPI_MODE1 // CPOL: 0, CPHA: 1
#define SPI_MODE_2 SPI_MODE2 // CPOL: 1, CPHA: 0
#define SPI_MODE_3 SPI_MODE3 // CPOL: 1, CPHA: 1

typedef struct {

    SPIClass *bus;
    uint8_t mode; // SPI Mode
    uint8_t bitOrder; // MSB or LSB
    uint32_t speed;
    uint8_t SS0;
    uint8_t SS1;
    uint8_t SS2;

} spi_handle_t; // Handler for SPI Peripheral

void hal_spi_init(spi_handle_t *, uint32_t);
void hal_spi_start(spi_handle_t *);
void hal_spi_assert_ss(spi_handle_t *, uint8_t);
void hal_spi_deassert_ss(spi_handle_t *, uint8_t);
void hal_spi_stop(spi_handle_t *);
void hal_spi_transfer(spi_handle_t *, uint8_t *, size_t);
void hal_spi_read_write(spi_handle_t *, uint8_t *, uint8_t *, size_t);

extern spi_handle_t SPI_0;
/* Teensyduino only wraps for SPI0 and doesn't allow for the other 3 buses.
extern spi_handle_t SPI_1;
extern spi_handle_t SPI_2;
*/

#endif
