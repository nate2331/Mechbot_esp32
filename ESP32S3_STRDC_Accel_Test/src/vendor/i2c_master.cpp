/*
 * I2C Master Library C++ File.
 *
 * @file        i2c_master.cpp
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


#include "i2c_master.h"
#include "timer.h"

/****************************************************************************
 * @brief Configure and open I2C peripheral
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param speed Speed of I2C bus. Defaults to 400kHz.
 ****************************************************************************/
void i2c_open(i2c_handle_t *handle, uint32_t speed = 400000)
{

    hal_i2c_init(handle, speed);

}

/****************************************************************************
 * @brief Check for response from I2C address. Sets a 500ms time out.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param address Address to check for response.
 * @return 0: for success
 *  1: Time out
 ****************************************************************************/
uint8_t i2c_find(i2c_handle_t *handle, uint8_t address)
{

    timer_handle_t watchdog;
    timer_init(&watchdog, 500000); // Set 500ms second timer so it doesn't spin indefinitely
    timer_start(&watchdog);

    hal_i2c_start(handle, address);

    while ((hal_i2c_stop(handle, true) != 0) && !timer_check_exp(&watchdog));

    return watchdog.isExp; // 0 if found, 1 if timer expired

}

/****************************************************************************
 * @brief Update the I2C Handle with a new address. Does not communicate on bus.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param address Address to store in handle.
 ****************************************************************************/
void i2c_set_addr(i2c_handle_t *handle, uint8_t address)
{
    handle->curr_addr = address;
}

/****************************************************************************
 * @brief Read I2C data.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param data Data to receive over I2C, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 65535 bytes (uint16_t).
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t i2c_read(i2c_handle_t *handle, uint8_t *data, uint16_t bytes)
{

    return hal_i2c_read(handle, data, bytes);

}

/****************************************************************************
 * @brief Write I2C data.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param data Data to send over I2C, must be a pointer to an array of bytes.
 * @param bytes Length of data in bytes, up to 256 bytes (uint8_t).
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t i2c_write(i2c_handle_t *handle, uint8_t *data, uint8_t bytes)
{

    uint8_t success;

    hal_i2c_start(handle, handle->curr_addr); // Start condition and send address
    hal_i2c_write(handle, data, bytes); // Write data of size bytes
    success = hal_i2c_stop(handle, true); // Send stop condition

    return success;

}

/****************************************************************************
 * @brief Read data from I2C Register. Write register address and then read data from register.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 * @param reg Register to read from, must be a pointer to an array of bytes (or a singular byte).
 * @param regBytes Length of register in bytes.
 * @param data Data to read over I2C, must be a pointer to an array of bytes.
 * @param dataBytes Length of data in bytes, up to 256 bytes (uint8_t).
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t i2c_read_reg(i2c_handle_t *handle, uint8_t *reg, uint8_t regBytes, uint8_t *data, uint16_t dataBytes)
{

    uint8_t success;

    hal_i2c_start(handle, handle->curr_addr); // Start condition and send address
    hal_i2c_write(handle, reg, regBytes); // Write data of size regBytes
    hal_i2c_stop(handle, false);
    success = i2c_read(handle, data, dataBytes); // Read data of size dataBytes

    return success;

}

/****************************************************************************
 * @brief Close I2C peripheral.
 * @param handle Pointer to i2c_handle_t which contains the relevant peripheral info.
 ****************************************************************************/
void i2c_close(i2c_handle_t *handle)
{

    hal_i2c_close(handle);

}