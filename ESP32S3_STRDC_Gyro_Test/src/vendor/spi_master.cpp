/*
 * SPI Master Library C++ File.
 *
 * @file        spi_master.cpp
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


#include "spi_master.h"
#include "timer.h"
#include "gpio.h"

/****************************************************************************
 * @brief Configure and open SPI peripheral. Set SS0 and any other specified SS.
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param speed Speed of SPI bus. Defaults to 5Mbps.
 * @param mode SPI Mode.
 * @param order MSB or LSB First.
 ****************************************************************************/
void spi_open(spi_handle_t *handle, uint32_t speed = 5000000, uint8_t mode = SPI_MODE_0, uint8_t order = SPI_BIT_ORDER_MSB)
{

    hal_spi_init(handle, speed);
    handle->mode = mode;
    handle->bitOrder = order;

}

/****************************************************************************
 * @brief Read data from SPI (full-duplex communication). This also sends the same length of data (cleared in this function).
 * @param slave SS pin numeration to address (e.g. 0, 1, 2).
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param data Data received over SPI, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 65535 bytes (uint16_t).
 ****************************************************************************/
void spi_read(spi_handle_t *handle, uint8_t slave, uint8_t *data, size_t bytes)
{

    memset(data, 0x00, bytes); // clear out existing buffer

    hal_spi_start(handle);

    hal_spi_assert_ss(handle, slave);

    hal_spi_transfer(handle, data, bytes); // This function writes data and returns any read data to the same pointer

    hal_spi_deassert_ss(handle, slave);

    hal_spi_stop(handle);
}

/****************************************************************************
 * @brief Write data in SPI (full-duplex communication). This also receives the same length of data.
 * @param slave SS pin numeration to address (e.g. 0, 1, 2).
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param data Data to send over SPI, will be overwritten by data received during send, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 65535 bytes (uint16_t).
 ****************************************************************************/
void spi_write(spi_handle_t *handle, uint8_t slave, uint8_t *data, size_t bytes)
{

    hal_spi_start(handle);

    hal_spi_assert_ss(handle, slave);

    hal_spi_transfer(handle, data, bytes); // This function writes data and returns any read data to the same pointer

    hal_spi_deassert_ss(handle, slave);

    hal_spi_stop(handle);

}

/****************************************************************************
 * @brief Write and receive data in SPI (full-duplex communication). Use this function to separate write and read data when full-duplex communication is desired.
 * @param slave SS pin numeration to address (e.g. 0, 1, 2).
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param data Data to send over SPI, must be a pointer to an array of bytes.
 * @param retData Data received during send, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 65535 bytes (uint16_t).
 ****************************************************************************/
void spi_write_read(spi_handle_t *handle, uint8_t slave, uint8_t *data, uint8_t *retData, size_t bytes)
{

    hal_spi_start(handle);

    hal_spi_assert_ss(handle, slave);

    hal_spi_read_write(handle, data, retData, bytes);

    hal_spi_deassert_ss(handle, slave);

    hal_spi_stop(handle);

}
