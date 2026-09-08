/*
 * Teensyduino SPI Master HAL C++ File.
 *
 * @file        hal_spi_master.cpp
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

#include "hal_spi_master.h"

// Define Handlers
spi_handle_t SPI_0 = {.bus = &SPI, .SS0 = SS, .SS1 = 255, .SS2 = 255}; // Initialize SS1 and SS2 to arbitrary value for change by user
/* Teensyduino only wraps for SPI0 and doesn't allow for the other 3 buses.
spi_handle_t SPI_1 = {.bus = &SPI1};
spi_handle_t SPI_2 = {.bus = &SPI2};
*/


/****************************************************************************
 * @brief Configure and open SPI peripheral. Set SS0 and any other specified SS.
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param speed Speed of SPI bus.
 ****************************************************************************/
void hal_spi_init(spi_handle_t *handle, uint32_t speed)
{

    handle->speed = speed;

    // Set up SS as OUTPUT and write HIGH
    pinMode(handle->SS0, OUTPUT);
    digitalWrite(handle->SS0, HIGH);

    
    // Set up other SS if used
    if (handle->SS1 != 255)
    {
        digitalWrite(handle->SS1, HIGH);
        pinMode(handle->SS1, OUTPUT);
    }
    if (handle->SS2 != 255)
    {
        digitalWrite(handle->SS2, HIGH);
        pinMode(handle->SS2, OUTPUT);
    }

    handle->bus->begin(); // This will automatically set up the default SS (10) as an OUTPUT

}

/****************************************************************************
 * @brief Take control of SPI bus.
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 ****************************************************************************/
void hal_spi_start(spi_handle_t *handle)
{
    handle->bus->beginTransaction(SPISettings(handle->speed, handle->bitOrder, handle->mode));
}

/****************************************************************************
 * @brief Assert SS pin(s).
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param ss SS pin numeration to assert (e.g. 0, 1, 2), 3 for all SS.
 ****************************************************************************/
void hal_spi_assert_ss(spi_handle_t *handle, uint8_t ss)
{

    if (ss == 0)
        digitalWrite(handle->SS0, LOW);
    else if (ss == 1)
        digitalWrite(handle->SS1, LOW);
    else if (ss == 2)
        digitalWrite(handle->SS2, LOW);
    else if (ss == 3) // All SS
    {
        digitalWrite(handle->SS0, LOW);
        digitalWrite(handle->SS1, LOW);
        digitalWrite(handle->SS2, LOW);
    }
    
}

/****************************************************************************
 * @brief Dessert SS pin(s).
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param ss SS pin numeration to deassert (e.g. 0, 1, 2), 3 for all SS.
 ****************************************************************************/
void hal_spi_deassert_ss(spi_handle_t *handle, uint8_t ss)
{

    if (ss == 0)
        digitalWrite(handle->SS0, HIGH);
    else if (ss == 1)
        digitalWrite(handle->SS1, HIGH);
    else if (ss == 2)
        digitalWrite(handle->SS2, HIGH);
    else if (ss == 3) // All SS
    {
        digitalWrite(handle->SS0, HIGH);
        digitalWrite(handle->SS1, HIGH);
        digitalWrite(handle->SS2, HIGH);
    }
    
}

/****************************************************************************
 * @brief Release control of SPI bus.
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 ****************************************************************************/
void hal_spi_stop(spi_handle_t *handle)
{
    handle->bus->endTransaction();
}

/****************************************************************************
 * @brief Write data and return data received (full-duplex communication).
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param data Data to send over SPI, will be overwritten by data received during send, must be a pointer to an array of bytes.
 * @param length Length of data in bytes, up to 65535 bytes (uint16_t).
 ****************************************************************************/
void hal_spi_transfer(spi_handle_t *handle, uint8_t *data, size_t length)
{
    handle->bus->transfer(data, length); // Sends and receives data from the same pointer
}

/****************************************************************************
 * @brief Write and receive data in SPI (full-duplex communication). Use this function to separate write and read data when full-duplex communication is desired.
 * @param handle Pointer to SPI handler which contains the relevant peripheral info.
 * @param data Data to send over SPI, must be a pointer to an array of bytes.
 * @param retData Data received during send, must be a pointer to an array of bytes.
 * @param length Length of data in bytes (size_t).
 ****************************************************************************/
void hal_spi_read_write(spi_handle_t *handle, uint8_t *data, uint8_t *retData, size_t length)
{
    handle->bus->transferBytes(data, retData, length);
}
