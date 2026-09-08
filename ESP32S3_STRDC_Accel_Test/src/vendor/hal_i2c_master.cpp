/*
 * Teensyduino I2C Master HAL C++ File.
 *
 * @file        hal_i2c_master.cpp
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

#include "hal_i2c_master.h"

// Define Handlers
i2c_handle_t i2c1 = {.bus = &Wire};
i2c_handle_t i2c2 = {.bus = &Wire1};

/****************************************************************************
 * @brief Configure and open I2C peripheral
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param speed Speed of I2C bus.
 ****************************************************************************/
void hal_i2c_init(i2c_handle_t *handle, uint32_t speed)
{

    // ESP32-S3 test wiring. The original transaction/chunking logic is unchanged.
    handle->speed = speed;
    if (handle->bus == &Wire)
        handle->bus->begin(8, 9, speed);
    else
    {
        // Configure Wire1 pins with Wire1.setPins() before opening that bus.
        handle->bus->begin();
        handle->bus->setClock(speed);
    }

}

/****************************************************************************
 * @brief Send I2C start condition and address
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param address Address to send.
 ****************************************************************************/
void hal_i2c_start(i2c_handle_t *handle, uint8_t address)
{

    // We request the address here because this function may be used for bus scanning and we don't want to have to change the handle's address each check
    handle->bus->beginTransmission(address);

}

/****************************************************************************
 * @brief Send I2C stop condition or end transmission.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param stop Boolean to indicate whether the stop condition should be sent.
 * @return 0: for success
 *  1: length too long for buffer
 *  2: address send, NACK received
 *  3: data send, NACK received
 *  4: other twi error (lost bus arbitration, bus error, ..)
 ****************************************************************************/
uint8_t hal_i2c_stop(i2c_handle_t *handle, bool stop = true)
{

    return handle->bus->endTransmission(stop);

}

/****************************************************************************
 * @brief Read I2C data. Reads data in max buffer size chunks. Sets a timeout for 100ms.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param data Data to receive over I2C, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 65535 bytes (uint16_t).
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t hal_i2c_read(i2c_handle_t *handle, uint8_t *data, uint16_t bytes)
{

    //uint16_t avail = 1;
    uint16_t count;
    volatile uint16_t bytesLeft = bytes;

    uint32_t timer = millis();

    while (bytesLeft != 0)
    {
        if (millis() - timer > 100) // Set timeout of 100ms
            return 1;

        if (bytesLeft > I2C_MAX_BUFFER_SIZE) // Don't request more than max buffer size
        {

            count = handle->bus->requestFrom(handle->curr_addr, (uint8_t)I2C_MAX_BUFFER_SIZE); // Request number of bytes from slave, send stop (otherwise we do a repeated start)
            
        }
        else
        {
            count = handle->bus->requestFrom(handle->curr_addr, (uint8_t)bytesLeft); // Request number of bytes from slave, send stop
            
        }

        //avail = handle->bus->available(); // Check what the returned number of bytes is

        for (uint16_t i = (bytes - bytesLeft); i < (count + (bytes - bytesLeft)); i++) // Read bytes available up to amount requested
        {

            data[i] = handle->bus->read();

        }

        bytesLeft -= count; // Track bytes read
    }

    return 0;
    
}

/****************************************************************************
 * @brief Write I2C data. This function implements the data send portion, not start, address, or stop.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param data Data to send over I2C, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 256 bytes (uint8_t).
 ****************************************************************************/
void hal_i2c_write(i2c_handle_t *handle, uint8_t *data, uint8_t bytes)
{

    handle->bus->write(data, bytes);

}

/****************************************************************************
 * @brief Close I2C peripheral.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param data Data to send over I2C, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 256 bytes (uint8_t).
 ****************************************************************************/
void hal_i2c_close(i2c_handle_t *handle)
{

    handle->bus->end();

}
