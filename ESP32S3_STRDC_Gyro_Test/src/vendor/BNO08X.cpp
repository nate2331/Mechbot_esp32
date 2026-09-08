/*
 * BNO08x Module C++ File.
 *
 * @file        BNO08X.cpp
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

#include "BNO08x.h"

#include <string.h>

#define DEBUG

#ifdef DEBUG
#include <Arduino.h>
#endif

// Sensor Length Map
uint8_t report_length[] = {
    0, // 0
    10, // 1
    10, // 2
    10, // 3
    10, // 4
    14, // 5
    10, // 6
    16, // 7
    12, // 8
    14, // 9
    8,// 10
    8, // 11
    6, // 12
    6, // 13
    6, // 14
    16, // 15
    5, // 16
    12, // 17
    6, // 18
    6, // 19
    6,// 20
    16, // 21
    16, // 22
    0, // 23
    8, // 24
    6, // 25
    6, // 26
    6, // 27
    6, // 28
    0, // 29
    16,// 30
    6, // 31
    6, // 32
    6, // 33
    6, // 34
    6, // 35
    0, // 36
    0, // 37
    0, // 38
    0, // 39
    14, // 40
    12, // 41
    14, // 42
    6, // 43
    24, // 44
    60 // 45
};

#ifdef UART_BUFFER_EXTRA
static uint8_t uart_buff[192]; // Additional buffer for UART (not as much as we need but it's better than nothing)
#endif

/****************************************************************************
 * Static Function Declarations
 ****************************************************************************/

// Utility Functions
static uint8_t bno08x_int_wait(bno08x_t *);

// Initialization
static uint8_t bno08x_SHTP_startup(bno08x_t *);
static uint8_t bno08x_hub_startup(bno08x_t *);
static uint8_t bno08x_get_ID(bno08x_t *);

// Messaging
static uint8_t bno08x_tx(bno08x_t *, uint8_t *, uint8_t);
static uint8_t bno08x_rx(bno08x_t *);
static uint8_t bno08x_cmd_request(bno08x_t *, uint8_t , uint8_t *);
static uint8_t bno08x_exec_request(bno08x_t *, uint8_t);
static uint8_t bno08x_feature_request(bno08x_t *, uint8_t, uint8_t *);
static void bno08x_parse_message(bno08x_t *);

// Response Handlers
static void bno08x_exec_response(bno08x_t *);
static void bno08x_SHTP_advert_response(bno08x_t *);
static uint8_t bno08x_cmd_response(bno08x_t *);
static uint8_t bno08x_feature_response(bno08x_t *);
static uint8_t bno08x_ID_response(bno08x_t *);
static uint8_t bno08x_flush_response(bno08x_t *);
static uint8_t bno08x_frs_response(bno08x_t *);
static void bno08x_bsn_response(bno08x_t *);

/****************************************************************************
 * Utility Functions
 ****************************************************************************/

/****************************************************************************
 * @brief Perform a hardware reset on the BNO08x using the Reset Pin.
 * @param handle Handle for BNO08x chip.
 ****************************************************************************/
void bno08x_hw_reset(bno08x_t *handle)
{

    gpio_write(handle->pinRst, HIGH);
    timer_blocking_delay(1000);
    gpio_write(handle->pinRst, LOW);
    timer_blocking_delay(100); // Only about 10ns is really necessary
    gpio_write(handle->pinRst, HIGH);
    timer_blocking_delay(100000); // Timing is about 100ms

}

/****************************************************************************
 * @brief Wait for interrupt helper function.
 * @param handle Handle for BNO08x chip.
 * @return 0: Read asserted interrupt successful
 *  1: Timed out
 ****************************************************************************/
static uint8_t bno08x_int_wait(bno08x_t *handle)
{

    timer_handle_t int_timer;
    timer_init(&int_timer, 50000); // 50ms
    timer_start(&int_timer);

    while (timer_check_exp(&int_timer) == 0)
    {
        if (gpio_read(handle->pinInt) == 0)
            return 0;
    }

    #ifdef DEBUG
    Serial.println("[DEBUG] Timed out waiting for interrupt");
    #endif
    return 1;

}


/****************************************************************************
 * Initialization
 ****************************************************************************/


/****************************************************************************
 * @brief Initialize blank configuration for BNO08x IC:
 *  Configure associated GPIO,
 *  Initialize comm peripheral (I2C, SPI),
 *  Perform HW Reset,
 *  Receive Initialization Messages,
 *  Get Product ID
 * @param handle Handle for BNO08x chip.
 * @return 0: Read asserted interrupt successful
 *  1: Failed to receive interrupt after reset
 *  2: Failed to Find I2C Device
 *  3: Failed to Receive Startup Advertisement
 *  4: Failed to Receive Hub Startup Message
 *  5: Failed to Get Product ID
 *  6: Failed to Receive Reset Complete
 ****************************************************************************/
uint8_t bno08x_init(bno08x_t *handle)
{
    
    // Initialize BNO08x Attributes
    handle->cmdSeq = 0;
    handle->ch1Seq = 0;
    handle->ch2Seq = 0;
    handle->isRst = false;
    handle->isSleep = false;
    handle->isInit = false;
    handle->advertRcd = false;
    handle->calSuccess = false;
    handle->saveDCD = false;

    handle->oscType = 255; // Set to arbitrary value

    // Register all calibration routines disabled
    handle->accelCal = 0;
    handle->gyroCal = 0;
    handle->magCal = 0;
    handle->planCal = 0;
    handle->tableCal = 0;
    
    // Initialize all reports
    for (uint8_t i = 1; i < (BNO08X_SENSOR_MOTION_REQUEST + 1); i++)
    {
        handle->reports[i].id = i;
        handle->reports[i].enabled = 0;
        handle->reports[i].newData = 0;

        handle->reports[i].countProd = 0;
        handle->reports[i].countRead = 0;
        handle->reports[i].filterPass = 0;
        handle->reports[i].thresholdPass = 0;
    }

    // Setup GPIO
    gpio_mode(handle->pinInt, INPUT_PULLUP);
    gpio_mode(handle->pinRst, OUTPUT);
    gpio_mode(handle->wakePin, OUTPUT);
    
    gpio_write(handle->pinRst, HIGH);

    timer_blocking_delay(100000);

    if(handle->busType == BNO08X_I2C)
    {
        
        if(i2c_find((i2c_handle_t*)handle->bus, handle->busAddr))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Failed to receive response from I2C address at startup");
            #endif
            return 2; // Didn't receive response from I2C Address
        }
        
        i2c_set_addr((i2c_handle_t*)handle->bus, handle->busAddr);

        gpio_write(handle->wakePin, GPIO_LOW); // Set P0 to Low

    }
    else if(handle->busType == BNO08X_SPI)
    {
        gpio_write(handle->wakePin, GPIO_HIGH); // Set P0 to High
    }
    else if(handle->busType == BNO08X_UART)
    {

        handle->writeAvail = 0;

        #ifdef UART_BUFFER_EXTRA
        serial_buffer_read_add((serial_handle_t*)handle->bus, uart_buff, 192);
        #endif

        gpio_write(handle->wakePin, GPIO_LOW); // Set P0 to Low
    }

    bno08x_hw_reset(handle);
    
    gpio_write(handle->wakePin, GPIO_HIGH); // Ensure wake pin isn't asserted
    
    if(handle->busType == BNO08X_SPI) // Used to be != BNO08X_UART, but this is likely unnecessary on I2C as well
    {
        if (bno08x_int_wait(handle))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Timeout waiting for interrupt");
            #endif
            return 1;
        }
    }
    
    if (bno08x_SHTP_startup(handle))
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Failed SHTP startup");
        #endif
        return 3;
    }

    if(handle->busType != BNO08X_UART)  // Downstream impact of buffer size - large size is only needed for SHTP, we can't read it all atm so it prevents proper segmentation from hub startup
    {
        if (bno08x_hub_startup(handle))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Failed Hub Startup");
            #endif
            return 4;
        }
    }

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 2500000); // 250ms
    timer_start(&gen_timer);

    while (!handle->isRst) // Wait for execution channel reset complete response
    {
        
        if (timer_check_exp(&gen_timer))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Failed waiting for execution channel reset");
            #endif
            return 6;
        }
        
        bno08x_get_messages(handle);

    }

    if (bno08x_get_ID(handle))
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Failed to get ID");
        #endif
        return 5;
    }
    
    return 0;

}

/****************************************************************************
 * @brief Send reinitialize command for Sensor Hub (not complete reset). Will require re-enabling sensors.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
/*
static uint8_t bno08x_hub_reinit(bno08x_t *handle)
{

    uint8_t params[] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0};

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms

    handle->isInit = false;

    bno08x_cmd_request(handle, BNO08X_CMD_INIT, params);

    timer_blocking_delay(250000);

    timer_start(&gen_timer);

    while (!handle->isInit)
    {
        if (timer_check_exp(&gen_timer))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Timed out waiting for initialization");
            #endif
            return 1;
        }

        bno08x_get_messages(handle);
    }

    return 0;

}
*/

/****************************************************************************
 * @brief Wait for SHTP Startup Advertisement - Required after reset.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
static uint8_t bno08x_SHTP_startup(bno08x_t *handle)
{
    // Receive SHTP unsolicited advertisement response to start sending commands

    timer_handle_t adv_timer;
    timer_init(&adv_timer, 250000); // 250ms
    timer_start(&adv_timer);

    while (!handle->advertRcd) // Wait for execution channel reset complete response
    {
        
        if (timer_check_exp(&adv_timer))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Timed out waiting to receive initial advertisement");
            #endif
            return 1;
        }
        
        bno08x_get_messages(handle);

    }

    memset(handle->buffer, 0x00, sizeof(handle->buffer)); // clear out existing buffer

    return 0;

}

/****************************************************************************
 * @brief Wait for Hub Initialization Message - Required after reset.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
static uint8_t bno08x_hub_startup(bno08x_t *handle)
{

    timer_handle_t adv_timer;
    timer_init(&adv_timer, 250000); // 250ms
    timer_start(&adv_timer);

    while (!handle->isInit) // Wait for all clear from command channel
    {
        
        if (timer_check_exp(&adv_timer))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Timed out waiting for all clear from command channel");
            #endif
            return 1;
        }
        
        bno08x_get_messages(handle);

    }

    memset(handle->buffer, 0x00, sizeof(handle->buffer)); // clear out existing buffer

    return 0;

}

/****************************************************************************
 * @brief Request Product ID and store data in handle.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out attempting to request ID
 *  2: Timed out waiting for ID response
 ****************************************************************************/
static uint8_t bno08x_get_ID(bno08x_t *handle)
{
    
    uint8_t header[4];

    uint8_t idCmd[6];

    header[0] = 6;
    header[1] = 0;
    header[2] = BNO08X_CHANNEL_SENSOR_CTRL;
    header[3] = handle->ch2Seq;

    memcpy(idCmd, header, 4);

    idCmd[4] = BNO08X_REPORT_PRODUCT_ID_REQUEST;
    idCmd[5] = 0;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    while(bno08x_tx(handle, idCmd, sizeof(idCmd)))
    {        
        if (timer_check_exp(&gen_timer))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Timed out attempting to request ID");
            #endif
            return 1;
        }

    }

    handle->ch2Seq++; // Increment transmission sequence

    timer_reset(&gen_timer);

    while(!handle->isID) // Wait for response
    {
        if (timer_check_exp(&gen_timer))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Timed out waiting for ID response");
            #endif
            return 2;
        }

        bno08x_get_messages(handle);
    }

    return 0;

}

/****************************************************************************
 * @brief Send Sleep Command.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out sending sleep command
 *  2: Timed out sending flush commands
 ****************************************************************************/
uint8_t bno08x_sleep(bno08x_t *handle)
{
    uint8_t j = 0;
    uint8_t flushReps[BNO08X_SENSOR_MOTION_REQUEST];
    
    // Grab all enabled sensor reports that will be disabled (or omit output) in sleep
    for (uint8_t i = 1; i < (BNO08X_SENSOR_MOTION_REQUEST + 1); i++)
    {
        if (handle->reports[i].enabled && !handle->reports[i].wakeupEn)
        {
            flushReps[j] = i;
            j++;
        }

    }

    // Send Sleep Command
    if (bno08x_exec_request(handle, BNO08X_EXEC_CMD_SLEEP))
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Failed to send sleep command");
        #endif
        return 1;
    }

    // Flush enabled sensors disabled by sleep - BNO08x has issues re-enabling sensors when the Get Feature Response is not read (particularly I2C)
    if (j > 0)
    {
        for (uint8_t t = 0; t < j; t++)
        {
            if (bno08x_flush(handle, flushReps[t]))
            {
                #ifdef DEBUG
                Serial.println("[DEBUG] Failed to execute flush command");
                #endif
                return 2;
            }
        }
    }

    return 0;
}

/****************************************************************************
 * @brief Send Wake Command.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_wake(bno08x_t *handle)
{
    return bno08x_exec_request(handle, BNO08X_EXEC_CMD_WAKE);
}


/****************************************************************************
 * Messaging
 ****************************************************************************/


/****************************************************************************
 * @brief Send message to BNO08x. Abstracts communication peripheral, handles wake pin, deals with duplex transfer and simultaneous message receive.
 * @param handle Handle for BNO08x chip.
 * @param data Data to send to BNO08x (array of bytes).
 * @param bytes Length of data to send (max 255).
 * @return 0: Success
 *  1: Timed out
 *  2: Failed to write I2C
 *  3: Failed to allocate memory for temp buffer
 *  4: Not enough buffer length for UART write
 ****************************************************************************/
static uint8_t bno08x_tx(bno08x_t *handle, uint8_t *data, uint8_t bytes)
{

    if(handle->busType == BNO08X_I2C)
    {
        if (handle->isSleep)
        {
            // Wake IC
            gpio_write(handle->wakePin, GPIO_LOW);
            timer_blocking_delay(50); // Wait min time for interrupt
        }

        i2c_set_addr((i2c_handle_t*)handle->bus, handle->busAddr); // Address this IC in case we're using this bus for multiple devices
        
        if (i2c_write((i2c_handle_t*)handle->bus, data, bytes))
            return 2;

        if (handle->isSleep)
            gpio_write(handle->wakePin, GPIO_HIGH);
    }
    else if(handle->busType == BNO08X_SPI)
    {
        uint8_t *tempBuff = (uint8_t *)malloc((1024) * sizeof(uint8_t)); // Create temporary buffer for read during duplex
        if (tempBuff == NULL)
            return 3;

        memset(tempBuff, 0x00, 2);

        memcpy(handle->buffer, data, (size_t)bytes);
        
        // Wake IC/Indicate intent to write data
        gpio_write(handle->wakePin, GPIO_LOW);
        timer_blocking_delay(50); // Wait min time for interrupt
        
        // Must wait for INT for duplex transfer
        if(bno08x_int_wait(handle))
        {

            gpio_write(handle->wakePin, GPIO_HIGH);
            free(tempBuff);
            return 1;
            
        }
        
        spi_write_read((spi_handle_t*)handle->bus, handle->busAddr, handle->buffer, tempBuff,(size_t)bytes);
        
        uint16_t length = (uint16_t)tempBuff[0] | ((uint16_t)tempBuff[1] << 8);

        if (length == 0xFFFF)
            length = 0; // If invalid length
        else
            length &= 0x7FFF; // Drop continuation bit

        // If header from data read during write indicates more data needs to be read
        if (length > bytes)
        {
            
            bno08x_rx(handle); // Read rest of message
            memcpy(tempBuff + bytes, handle->buffer + 4, length - bytes); // Store rest of message in temporary buffer
            memcpy(handle->buffer, tempBuff, length); // Copy from tempBuff to working buffer

        }
        else if (length > 0)
        {
            memcpy(handle->buffer, tempBuff, length);
        }

        free(tempBuff); // Free memory allocated for temporary buffer


        // Let IC go back to sleep
        gpio_write(handle->wakePin, GPIO_HIGH);

        if (length > 0)
            bno08x_parse_message(handle);

    }
    else if(handle->busType == BNO08X_UART)
    {
        // Format data to send
        uint8_t flag = 0x7E;
        uint8_t flagComp = 0; // Count of additional data to send if data needs to be modified if it contains a byte == flag (See SHTP Manual)

        for (uint8_t p = 0; p < bytes; p++)
        {
            if (data[p+flagComp] == flag)
            {
                data[p+flagComp] = 0x7D;
                flagComp++;
                data[p+flagComp] = 0x5E;
            }
        }

        // Create BSQ
        uint8_t bsq[] = {flag, 0x00, flag};

        if (handle->isSleep)
        {
            // Wake IC
            gpio_write(handle->wakePin, GPIO_LOW);
            timer_blocking_delay(50); // Wait min time for interrupt
        }

        handle->bsnResp = false;

        timer_handle_t gen_timer;
        timer_init(&gen_timer, 250000); // 250ms

        // Send BSQ
        for (uint8_t t = 0; t < 3; t++)
        {
            serial_write((serial_handle_t*)handle->bus, &bsq[t], 1);
            timer_blocking_delay(100);
        }

        if (handle->isSleep)
            gpio_write(handle->wakePin, GPIO_LOW);

        timer_start(&gen_timer);

        // Wait for BSN Response
        while (!handle->bsnResp)
        {
            if (timer_check_exp(&gen_timer))
                return 1;

            bno08x_get_messages(handle);

        }

        if (handle->writeAvail == 0)
            return 4;

        if (handle->writeAvail < bytes + flagComp)
            return 4;

        if (handle->isSleep)
        {
            // Wake IC
            gpio_write(handle->wakePin, GPIO_LOW);
            timer_blocking_delay(50); // Wait min time for interrupt
        }

        uint8_t protocol = 0x01; // Protocol ID SHTP

        serial_write((serial_handle_t*)handle->bus, &flag, 1); // Send Flag (start)
        timer_blocking_delay(100); // Wait 100us between bytes per datasheet

        serial_write((serial_handle_t*)handle->bus, &protocol, 1); // Send Prototcol ID
        timer_blocking_delay(100); // Wait 100us between bytes per datasheet

        for (uint8_t j = 0; j < bytes + flagComp; j++)
        {
            serial_write((serial_handle_t*)handle->bus, &data[j], 1); // Send data byte
            timer_blocking_delay(100); // Wait 100us between bytes per datasheet
        }

        serial_write((serial_handle_t*)handle->bus, &flag, 1); // Send Flag (end)

        if (handle->isSleep)
            gpio_write(handle->wakePin, GPIO_HIGH);

    }

    return 0;
}

/****************************************************************************
 * @brief Receive data from BNO08x and store in handle buffer. Abstracts communication peripheral, handles wake pin.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 *  2: Invalid Length (Likely unresponsive)
 *  3: Length is 0
 *  4: Invalid UART start (!flag)
 ****************************************************************************/
static uint8_t bno08x_rx(bno08x_t *handle)
{

    memset(handle->buffer, 0x00, sizeof(handle->buffer));
    
    uint16_t length = 0;
    uint16_t bytesRead = 0;
    uint8_t toRead = 0;

    uint8_t buff[I2C_MAX_BUFFER_SIZE];

    if(handle->busType == BNO08X_I2C)
    {
        // Check if BNO08x is asleep
        if (handle->isSleep)
        {
            // Wake IC
            gpio_write(handle->wakePin, GPIO_LOW);
            timer_blocking_delay(50); // Wait min time for interrupt
        }
        /* This is likely unnecessary, removing temporarily
        // Check if BNO08x has data to send
        if (bno08x_int_wait(handle))
        {
            if (handle->isSleep)
                gpio_write(handle->wakePin, GPIO_HIGH);
            return 1;
        }
        */
        i2c_set_addr((i2c_handle_t*)handle->bus, handle->busAddr); // Address this IC in case we're using this bus for multiple devices

        if (i2c_read((i2c_handle_t*)handle->bus, handle->buffer, 2))
        {
            if (handle->isSleep)
                gpio_write(handle->wakePin, GPIO_HIGH);
            return 1;
        }

        if (handle->isSleep)
            gpio_write(handle->wakePin, GPIO_HIGH);
        
        length = (uint16_t)handle->buffer[0] | ((uint16_t)handle->buffer[1] << 8);

        if (length == 0xFFFF) // If invalid length
            return 2;
        
        length &= 0x7FFF; // Drop continuation bit

        if (length == 0) // Don't read if no length
            return 3;

        if (length > BNO08X_BUFFER_SIZE)
            length = BNO08X_BUFFER_SIZE; // Change this to some type of return error
        
        while (length - bytesRead > 0)
        {
            // Read largest message buffer allows, accounting for header on messages after the first
            toRead = length - (bytesRead > 0 ? bytesRead - 4 : 0) > I2C_MAX_BUFFER_SIZE ? I2C_MAX_BUFFER_SIZE : length - (bytesRead > 0 ? bytesRead - 4 : 0);
            
            if (handle->isSleep)
            {
                // Wake IC
                gpio_write(handle->wakePin, GPIO_LOW);
                timer_blocking_delay(50); // Wait min time for interrupt
            }
            /* This is likely unnecessary, removing temporarily
            // Check if BNO08x has data to send
            if (bno08x_int_wait(handle))
            {
                if (handle->isSleep)
                    gpio_write(handle->wakePin, GPIO_HIGH);
                return 1;
            }
            */
            if (i2c_read((i2c_handle_t*)handle->bus, buff, toRead)) // MSB indicates if more data needs to be read
            {
                if (handle->isSleep)
                    gpio_write(handle->wakePin, GPIO_HIGH);
                return 1;
            }

            if (handle->isSleep)
                gpio_write(handle->wakePin, GPIO_HIGH);

            // Copy the first header in case we need it, but remove subsequent headers
            memcpy(handle->buffer + bytesRead, buff + (bytesRead > 0 ? 4 : 0), toRead - (bytesRead > 0 ? 4 : 0));

            // Update read count
            bytesRead += toRead - (bytesRead > 0 ? 4 : 0);

        }

    }
    else if(handle->busType == BNO08X_SPI)
    {

        // Wake IC
        gpio_write(handle->wakePin, GPIO_LOW);
        timer_blocking_delay(50); // Wait min time for interrupt

        // Check if BNO08x has data to send
        if (bno08x_int_wait(handle))
        {
            gpio_write(handle->wakePin, GPIO_HIGH);
            return 1;
        }

        spi_read((spi_handle_t*)handle->bus, handle->busAddr, handle->buffer, 4);

        gpio_write(handle->wakePin, GPIO_HIGH);

        length = ((uint16_t)handle->buffer[0] | ((uint16_t)handle->buffer[1] << 8)); // Get length

        if (length == 0xFFFF) // Check if invalid length
            return 2;

        length &= 0x7FFF; // Drop final bit for unread message

        if (length == 0) // Don't read if no length
            return 3;        

        gpio_write(handle->wakePin, GPIO_LOW);
        timer_blocking_delay(50); // Wait min time for interrupt

        // Check if BNO08x is ready
        if (bno08x_int_wait(handle))
        {
            gpio_write(handle->wakePin, GPIO_HIGH);
            return 1;
        }

        spi_read((spi_handle_t*)handle->bus, handle->busAddr, handle->buffer, length);

        // Let IC go back to sleep
        gpio_write(handle->wakePin, GPIO_HIGH);

    }
    else if(handle->busType == BNO08X_UART)
    {

        /* SHTP over UART

        0x7E (Flag Byte) is used to start/stop every frame
        0x7D (Control Escape) is used when a data byte would be 0x7E to indicate it's not a flag byte -
        If a data byte is 0x7E, 0x7D is sent followed by the data byte xor'd with 0x20 (so instead of 0x7E -> 0x7D, 0x5E)

        Message structure:
        0 - Flag
        1 - Protocol ID
        2 - N-1 - Data
        N - Flag

        Protocol ID:
        0x00 - SHTP over UART Control
        0x01 - SHTP (Data)

        SHTP over UART Control:
        Establish when it's safe for host to perform a write

        Buffer Status Query (BSQ): Sent from host to hub to inquire available buffer size for writing
        0 - Flag (0x7E)
        1 - Protocol ID (0x00)
        2 - Flag (0x7E)
        

        Buffer Status Notification (BSN): Sent from hub to host in response to BSQ. Bytes available are the cargo only
        0 - Flag (0x7E)
        1 - Protocol ID (0x00)
        2 - Bytes Available LSB
        3 - Bytes Available MSB
        4 - Flag (0x7E)

        Reads:
        Can occur at any time. Hub will assert HINT before transmission and may deassert at any time during transmission.

        Writes:
        Must send BSQ and ensure BSN indicates enough buffer is available for write. Have to delay a minimum of 100us between bytes written.
        
        */
        uint16_t j = 0;

        timer_handle_t gen_timer;
        timer_init(&gen_timer, 500000); // 500ms

        // Check if BNO08x is asleep
        if (handle->isSleep)
        {
            // Wake IC
            gpio_write(handle->wakePin, GPIO_LOW);
            timer_blocking_delay(50); // Wait min time for interrupt
        }

        // Check if BNO08x has data to send
        /* Interrupt wait is working sporadically, likely caused by read buffer size being too small
        if (bno08x_int_wait(handle))
        {
            if (handle->isSleep)
                gpio_write(handle->wakePin, GPIO_HIGH);
            
            return 1;
        }
        */

        // Read single byte
        serial_read((serial_handle_t*)handle->bus, handle->buffer, 1);

        if (handle->buffer[0] == 0x7E) // Check if it's a flag (start)
        {

            serial_read((serial_handle_t*)handle->bus, &handle->buffer[j], 1);

            if (handle->buffer[j] == 0x7E) // Check for repeated flag on initial byte only
                serial_read((serial_handle_t*)handle->bus, &handle->buffer[j], 1); // If so, read again for first byte

            timer_start(&gen_timer);

            while (handle->buffer[j] != 0x7E) // Read till end of message
            {

                if (timer_check_exp(&gen_timer))
                    break;
                
                j++; // Increment to keep track on buffer

                serial_read((serial_handle_t*)handle->bus, &handle->buffer[j], 1);

            }
            

        }
        else // Haven't received start message
        {
            if (handle->isSleep)
                gpio_write(handle->wakePin, GPIO_HIGH);

            return 4;
        }

        if (handle->isSleep)
            gpio_write(handle->wakePin, GPIO_HIGH);

    }

    return 0;

}

/****************************************************************************
 * @brief Send command request message on command channel to BNO08x.
 * @param handle Handle for BNO08x chip.
 * @param cmd Sub-command ID to send.
 * @param params 9 parameters required for sub-command (Pointer to array of bytes).
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
static uint8_t bno08x_cmd_request(bno08x_t *handle, uint8_t cmd, uint8_t *params)
{

    uint8_t header[4];

    uint8_t cmdReq[16];

    header[0] = sizeof(cmdReq); // Length LSB
    header[1] = 0; // Length MSB
    header[2] = BNO08X_CHANNEL_SENSOR_CTRL;
    header[3] = handle->ch2Seq;

    memcpy(cmdReq, header, sizeof(header));

    cmdReq[4] = BNO08X_REPORT_COMMAND_REQUEST;
    cmdReq[5] = handle->cmdSeq;
    cmdReq[6] = cmd;

    memcpy(cmdReq + 7, params, 9);

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    
    while(bno08x_tx(handle, cmdReq, sizeof(cmdReq)))
    {        
        if (timer_check_exp(&gen_timer))
            return 1;

    }

    handle->cmdSeq++;
    handle->ch2Seq++;

    return 0;

}

/****************************************************************************
 * @brief Send executable command request message on executable channel to BNO08x.
 * @param handle Handle for BNO08x chip.
 * @param cmd Command ID to send.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
static uint8_t bno08x_exec_request(bno08x_t *handle, uint8_t cmd)
{

    uint8_t header[4];

    uint8_t execReq[5];

    header[0] = sizeof(execReq); // Length LSB
    header[1] = 0; // Length MSB
    header[2] = 1;
    header[3] = handle->ch1Seq;
    
    memcpy(execReq, header, sizeof(header));

    execReq[4] = cmd;

    timer_handle_t adv_timer;
    timer_init(&adv_timer, 250000); // 250ms
    timer_start(&adv_timer);
    
    while(bno08x_tx(handle, execReq, sizeof(execReq)))
    {
        if (timer_check_exp(&adv_timer))
                return 1;
    }
    
    handle->ch1Seq++;

    if (cmd == 3)
        handle->isSleep = true;
    else if (cmd == 2)
        handle->isSleep = false;

    return 0;

}

/****************************************************************************
 * @brief Interpret singular message and send to appropriate message handler function or to appropriate buffer.
 * @param handle Handle for BNO08x chip.
 ****************************************************************************/
static void bno08x_parse_message(bno08x_t *handle)
{

    if (handle->busType == BNO08X_UART)
    {
        if (handle->buffer[0] == 0) // SHTP over UART Control
        {
            bno08x_bsn_response(handle);
            return;
        }
        else // SHTP with Protocol ID prepended
            memmove(handle->buffer, handle->buffer + 1, sizeof(handle->buffer) - 1 * sizeof(uint8_t)); // Drop Protocol ID

    }

    uint16_t length;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms

    length = ((uint16_t)handle->buffer[0] + ((uint16_t)handle->buffer[1] << 8)) & 0x7FFF; // Drop continuation bit

    if (length == 0) // Error, no message
        return;
    
    // Handle Responses
    if (handle->buffer[2] == BNO08X_CHANNEL_SHTP_CMD)
    {
        if (handle->buffer[4] == 0)
            bno08x_SHTP_advert_response(handle);
    }
    else if (handle->buffer[2] == BNO08X_CHANNEL_EXECUTABLE)
        bno08x_exec_response(handle);
    else if (handle->buffer[2] == BNO08X_CHANNEL_SENSOR_CTRL)
    {
        uint8_t bytesRead = 0;
        memcpy(handle->cmdBuffer, handle->buffer + 4, length - 4); // Copy everything after header

        timer_start(&gen_timer);

        // Read through message and process each report
        while (length - bytesRead - 4 > 0)
        {

            if (timer_check_exp(&gen_timer))
                return;
            
            if (handle->cmdBuffer[0] == BNO08X_REPORT_FLUSH_COMPLETE)
                bytesRead += bno08x_flush_response(handle);
            else if (handle->cmdBuffer[0] == BNO08X_REPORT_COMMAND_RESPONSE)
                bytesRead += bno08x_cmd_response(handle);
            else if ((handle->cmdBuffer[0] == BNO08X_REPORT_FRS_READ_RESPONSE) || (handle->cmdBuffer[0] == BNO08X_REPORT_FRS_WRITE_RESPONSE))
                bytesRead += bno08x_frs_response(handle);
            else if (handle->cmdBuffer[0] == BNO08X_REPORT_PRODUCT_ID_RESPONSE)
                bytesRead += bno08x_ID_response(handle);
            else if (handle->cmdBuffer[0] == BNO08X_REPORT_GET_FEATURE_RESPONSE)
                bytesRead += bno08x_feature_response(handle);

            memcpy(handle->cmdBuffer, handle->buffer + bytesRead + 4, length - bytesRead - 4);

        }
    }
    else if (handle->buffer[2] == BNO08X_CHANNEL_GYRO) // Gyro-Integrated Rotation Vector report
    {

        int baseDelta = 0;
        uint16_t readLength = 4; // Skip SHTP
        uint8_t reportID;

        timer_start(&gen_timer);

        while (readLength < length)
        {
            if (timer_check_exp(&gen_timer))
                return;

            reportID = handle->buffer[readLength];
            if (reportID == BNO08x_REPORT_TIMEBASE_REFERENCE)
            {
                
                baseDelta = -((uint32_t)handle->buffer[readLength + 1] + ((uint32_t)handle->buffer[readLength + 2] << 8) + 
                ((uint32_t)handle->buffer[readLength + 3] << 16) + ((uint32_t)handle->buffer[readLength + 4] << 24));

                readLength += 5;

            }
            else if(reportID == BNO08x_REPORT_TIMESTAMP_REBASE)
            {

                baseDelta += (uint32_t)handle->buffer[readLength + 1] + ((uint32_t)handle->buffer[readLength + 2] << 8) + 
                ((uint32_t)handle->buffer[readLength + 3] << 16) + ((uint32_t)handle->buffer[readLength + 4] << 24);

                readLength += 5;

            }
            else
            {
                reportID = BNO08X_SENSOR_GYRO_INT_ROT_VECTOR;

                memcpy(handle->reports[reportID].dataBuff, handle->buffer + readLength, report_length[reportID]);

                handle->reports[reportID].newData = 1;

                handle->reports[reportID].timeDelay = baseDelta + ((((uint32_t)handle->buffer[readLength + 2] & 0xFC) << 6) | 
                    (uint32_t)handle->buffer[readLength + 3]);

                readLength += report_length[reportID];

            }

        }
    }
    else if (handle->buffer[2] > BNO08X_CHANNEL_SENSOR_CTRL) // Sensor report
    {
        /*
        SHTP Header: 4
        Timebase Reference: 5
        Report 1: X1
        Report 2: X2
        Rebase Reference: 5
        Report 3: X3
        */

        int baseDelta = 0;
        uint16_t readLength = 4; // Skip SHTP
        uint8_t reportID;

        timer_start(&gen_timer);

        while (readLength < length)
        {
            if (timer_check_exp(&gen_timer))
                return;

            reportID = handle->buffer[readLength];
            if (reportID == BNO08x_REPORT_TIMEBASE_REFERENCE)
            {
                // 100 us ticks
                baseDelta = -((uint32_t)handle->buffer[readLength + 1] + ((uint32_t)handle->buffer[readLength + 2] << 8) + 
                ((uint32_t)handle->buffer[readLength + 3] << 16) + ((uint32_t)handle->buffer[readLength + 4] << 24));

                readLength += 5;

            }
            else if(reportID == BNO08x_REPORT_TIMESTAMP_REBASE)
            {
                // 100 us ticks
                baseDelta += (uint32_t)handle->buffer[readLength + 1] + ((uint32_t)handle->buffer[readLength + 2] << 8) + 
                ((uint32_t)handle->buffer[readLength + 3] << 16) + ((uint32_t)handle->buffer[readLength + 4] << 24);

                readLength += 5;

            }
            else
            {

                if (report_length[reportID] == 0) // Check for erroneous data
                    return; // Avoid infinite loop due to 0 length

                memcpy(handle->reports[reportID].dataBuff, handle->buffer + readLength, report_length[reportID]);

                handle->reports[reportID].newData = 1;

                handle->reports[reportID].timeDelay = baseDelta + ((((uint32_t)handle->buffer[readLength + 2] & 0xFC) << 6) | 
                    (uint32_t)handle->buffer[readLength + 3]);

                readLength += report_length[reportID];

            }

        }

    }

}

/****************************************************************************
 * @brief Receive messages and then parse them. Should be flagged to be called when interrupt is triggered.
 * @param handle Handle for BNO08x chip.
 ****************************************************************************/
void bno08x_get_messages(bno08x_t *handle)
{

    if (bno08x_rx(handle)) // This should fix UART but might blow up everything else
        return;

    bno08x_parse_message(handle);

}

/****************************************************************************
 * @brief Request BNO08x to report all outstanding data for a sensor regardless of batch settings.
 * @param handle Handle for BNO08x chip.
 * @param sensor Sensor ID to be flushed.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_flush(bno08x_t *handle, uint8_t sensor)
{

    uint8_t header[4];

    uint8_t req[6];

    header[0] = sizeof(req); // Length LSB
    header[1] = 0; // Length MSB
    header[2] = BNO08X_CHANNEL_SENSOR_CTRL;
    header[3] = handle->ch2Seq;

    memcpy(req, header, sizeof(header));

    req[4] = BNO08X_REPORT_FORCE_FLUSH;
    req[5] = sensor;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);
    
    while(bno08x_tx(handle, req, sizeof(req)))
    {
        if (timer_check_exp(&gen_timer))
                return 1;
    }

    handle->ch2Seq++; // Increment sequence

    timer_reset(&gen_timer);

    while(!handle->flushComplete) // Wait for response
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        bno08x_get_messages(handle);
    }

    return 0;

}


/****************************************************************************
 * Response Handlers
 ****************************************************************************/


/****************************************************************************
 * @brief Called by Parse Messages. Checks response of messages from executable channel.
 * @param handle Handle for BNO08x chip.
 ****************************************************************************/
static void bno08x_bsn_response(bno08x_t *handle)
{

    handle->bsnResp = true; // Indicate received response

    handle->writeAvail =  handle->buffer[1] | (handle->buffer[2] << 8); // Return bytes available

}

/****************************************************************************
 * @brief Called by Parse Messages. Checks response of messages from executable channel.
 * @param handle Handle for BNO08x chip.
 ****************************************************************************/
static void bno08x_exec_response(bno08x_t *handle)
{

    if(handle->buffer[4] == 1)
        handle->isRst = true; // Indicate received response

}

/****************************************************************************
 * @brief Called by Parse Messages. Records data from SHTP Advertisement.
 * @param handle Handle for BNO08x chip.
 ****************************************************************************/
static void bno08x_SHTP_advert_response(bno08x_t *handle)
{
    
    uint8_t currChan = 0;
    uint32_t currGuid = 0;
    char currName[15];

    uint16_t i = 5; // Start from after command response type

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    while (i < ((uint16_t)handle->buffer[0] | (((uint16_t)handle->buffer[1] & 0x7F) << 8)))
    {
        
        if (timer_check_exp(&gen_timer))
            break;

        switch (handle->buffer[i])
        {
            case 0: // Reserved
                i += handle->buffer[i+1] + 2;
                break;
            case 1: // GUID
                currGuid = (uint32_t)handle->buffer[i+2] | ((uint32_t)handle->buffer[i+3] << 8) | ((uint32_t)handle->buffer[i+4] << 16) | ((uint32_t)handle->buffer[i+5] << 24);
                i += handle->buffer[i+1] + 2;
                break;
            case 2: // Max Cargo Plus Header Write
                handle->metaData.maxWrite = (uint16_t)handle->buffer[i+2] | ((uint16_t)handle->buffer[i+3] << 8);
                i += handle->buffer[i+1] + 2;
                break;
            case 3: // Max Cargo Plus Header Read
                handle->metaData.maxRead = (uint16_t)handle->buffer[i+2] | ((uint16_t)handle->buffer[i+3] << 8);
                i += handle->buffer[i+1] + 2;
                break;
            case 4: // Max Transfer Write
                handle->metaData.maxTransferWrite = (uint16_t)handle->buffer[i+2] | ((uint16_t)handle->buffer[i+3] << 8);
                i += handle->buffer[i+1] + 2;
                break;
            case 5: // Max Transfer Read
                handle->metaData.maxTransferRead = (uint16_t)handle->buffer[i+2] | ((uint16_t)handle->buffer[i+3] << 8);
                i += handle->buffer[i+1] + 2;
                break;
            case 6: // Normal Channel
                currChan = handle->buffer[i+2];
                memcpy(handle->metaData.chan[currChan].appName, currName, sizeof(currName));
                handle->metaData.chan[currChan].wakeChan = false;
                handle->metaData.chan[currChan].guid = currGuid;
                i += handle->buffer[i+1] + 2;
                break;
            case 7: // Wake Channel
                currChan = handle->buffer[i+2];
                memcpy(handle->metaData.chan[currChan].appName, currName, sizeof(currName));
                handle->metaData.chan[currChan].wakeChan = true;
                handle->metaData.chan[currChan].guid = currGuid;
                i += handle->buffer[i+1] + 2;
                break;
            case 8: // Application Name
                for (uint8_t j = 0; j < handle->buffer[i+1]; j++)
                {
                    currName[j] = handle->buffer[i+2+j]; // ASCII
                }
                i += handle->buffer[i+1] + 2;
                break;
            case 9: // Channel Name
                for (uint8_t j = 0; j < handle->buffer[i+1]; j++)
                {
                    handle->metaData.chan[currChan].chanName[j] = handle->buffer[i+2+j]; // ASCII
                }
                i += handle->buffer[i+1] + 2;
                break;
            case 0x80: // SHTP Version
                for (uint8_t j = 0; j < handle->buffer[i+1]; j++)
                {
                    handle->metaData.shtpVer[j] = handle->buffer[i+2+j]; // ASCII
                }
                i += handle->buffer[i+1] + 2;
                break;
            case 0x81: // SHTP UART Timeout
                handle->metaData.shtpUartTO = (uint32_t)handle->buffer[i+2] | ((uint32_t)handle->buffer[i+3] << 8) | ((uint32_t)handle->buffer[i+4] << 16) | ((uint32_t)handle->buffer[i+5] << 24);
                i += handle->buffer[i+1] + 2;
                break;
            default: // Error, cycle till next TLV
                while (handle->buffer[i] != 0)
                {
                    i++;
                    if (timer_check_exp(&gen_timer))
                        break;
                }
                i++;
                break;

        }
    }

    handle->advertRcd = true; // Indicate received response

}

/****************************************************************************
 * @brief Called by Parse Messages. Handles responses of any command requests.
 * @param handle Handle for BNO08x chip.
 * @return Size of message
 ****************************************************************************/
static uint8_t bno08x_cmd_response(bno08x_t *handle)
{
    
    if ((handle->cmdBuffer[2] & 0x0F) == BNO08X_CMD_ERRORS)
        handle->isErr = true; // Indicate received response
    else if ((handle->cmdBuffer[2] & 0x0F) == BNO08X_CMD_COUNTER)
    {
        if (handle->cmdBuffer[6] == 1)
        {

            uint8_t id = handle->cmdBuffer[5];

            if (handle->cmdBuffer[4] == 1)
            {
                handle->reports[id].countRead = (uint32_t)handle->cmdBuffer[8] | ((uint32_t)handle->cmdBuffer[9] << 8) |
                 ((uint32_t)handle->cmdBuffer[10] << 16) | ((uint32_t)handle->cmdBuffer[11] << 24);
                handle->reports[id].thresholdPass = (uint32_t)handle->cmdBuffer[12] | ((uint32_t)handle->cmdBuffer[13] << 8) |
                 ((uint32_t)handle->cmdBuffer[14] << 16) | ((uint32_t)handle->cmdBuffer[15] << 24);
            }
            else if (handle->cmdBuffer[4] == 0)
            {
                handle->reports[id].countProd = (uint32_t)handle->cmdBuffer[8] | ((uint32_t)handle->cmdBuffer[9] << 8) |
                 ((uint32_t)handle->cmdBuffer[10] << 16) | ((uint32_t)handle->cmdBuffer[11] << 24);
                handle->reports[id].filterPass = (uint32_t)handle->cmdBuffer[12] | ((uint32_t)handle->cmdBuffer[13] << 8) |
                 ((uint32_t)handle->cmdBuffer[14] << 16) | ((uint32_t)handle->cmdBuffer[15] << 24);
            }

            handle->rspCount = true; // Indicate received response

        }

    }
    else if ((handle->cmdBuffer[2] & 0x0F) == BNO08X_CMD_INIT)
    {
        if (handle->cmdBuffer[5] == 0)
            handle->isInit = true; // Indicate received response
    }
    else if ((handle->cmdBuffer[2] & 0x0F) == BNO08X_CMD_DCD_SAVE)
    {
        if (handle->cmdBuffer[5] == 0x00)
            handle->saveDCD = true; // Indicate received response
    }
    else if ((handle->cmdBuffer[2] & 0x0F) == BNO08X_CMD_MECAL)
    {
        if (handle->cmdBuffer[5] == 0x00)
            handle->calSuccess = true; // Indicate received response

        handle->accelCal = handle->cmdBuffer[6];
        handle->gyroCal = handle->cmdBuffer[7];
        handle->magCal = handle->cmdBuffer[8];
        handle->planCal = handle->cmdBuffer[9];
        handle->tableCal = handle->cmdBuffer[10];
    }
    else if ((handle->cmdBuffer[2] & 0x0F) == BNO08X_CMD_OSC)
    {
        handle->oscType = handle->cmdBuffer[5];
    }
    else if ((handle->cmdBuffer[2] & 0x0F) == BNO08X_CMD_CAL)
    {
        handle->startCal = handle->cmdBuffer[5];
        handle->calStatus = handle->cmdBuffer[6];
    }

    return 16; // Size of CMD Response

}

/****************************************************************************
 * @brief Called by Parse Messages. Handles Get Feature Responses.
 * @param handle Handle for BNO08x chip.
 * @return Size of message
 ****************************************************************************/
static uint8_t bno08x_feature_response(bno08x_t *handle)
{

    uint8_t reportID = handle->cmdBuffer[1];
    uint8_t flags = handle->cmdBuffer[2];
    uint32_t repInterval = (uint32_t)handle->cmdBuffer[5] | ((uint32_t)handle->cmdBuffer[6] << 8) | ((uint32_t)handle->cmdBuffer[7] << 16) | ((uint32_t)handle->cmdBuffer[8] << 24);
    uint32_t batchInterval = (uint32_t)handle->cmdBuffer[9] | ((uint32_t)handle->cmdBuffer[10] << 8) | ((uint32_t)handle->cmdBuffer[11] << 16) | ((uint32_t)handle->cmdBuffer[12] << 24);

    handle->reports[reportID].enabled = (repInterval > 0) || (batchInterval > 0);

    handle->reports[reportID].sensType = flags & 0x01;

    handle->reports[reportID].wakeupEn = (flags & 0x04) >> 2;

    handle->reports[reportID].sensEn = (flags & 0x02) >> 1;

    handle->reports[reportID].alwaysonEn = (flags & 0x08) >> 3;


    handle->reports[reportID].respRcd = true; // Indicate received response

    memset(handle->cmdBuffer, 0x00, sizeof(handle->cmdBuffer));

    return 17; // Size of Get Feature Response

}

/****************************************************************************
 * @brief Called by Parse Messages. Records data from Get ID Response.
 * @param handle Handle for BNO08x chip.
 * @return Size of message
 ****************************************************************************/
static uint8_t bno08x_ID_response(bno08x_t *handle)
{

    handle->metaData.rstCause = handle->cmdBuffer[1];
    handle->metaData.swMaj = handle->cmdBuffer[2];
    handle->metaData.swMin = handle->cmdBuffer[3];
    handle->metaData.swPN = (uint32_t)handle->cmdBuffer[4] | ((uint32_t)handle->cmdBuffer[5] << 8) | ((uint32_t)handle->cmdBuffer[6] << 16) | ((uint32_t)handle->cmdBuffer[7] << 24);
    handle->metaData.swBN = (uint32_t)handle->cmdBuffer[8] | ((uint32_t)handle->cmdBuffer[9] << 8) | ((uint32_t)handle->cmdBuffer[10] << 16) | ((uint32_t)handle->cmdBuffer[11] << 24);
    handle->metaData.swPatch = (uint16_t)handle->cmdBuffer[12] | ((uint16_t)handle->cmdBuffer[13] << 8);
            
    handle->isID = true; // Indicate received response

    return 16; // Size of CMD Response

}

/****************************************************************************
 * @brief Called by Parse Messages. Checks Flush Complete Message.
 * @param handle Handle for BNO08x chip.
 * @return Size of message
 ****************************************************************************/
static uint8_t bno08x_flush_response(bno08x_t *handle)
{

    handle->flushComplete = true; // Indicate received response

    return 2; // Size of Flush Completed

}

/****************************************************************************
 * @brief Called by Parse Messages. Copies contents to FRS Buffer.
 * @param handle Handle for BNO08x chip.
 * @return Size of message
 ****************************************************************************/
static uint8_t bno08x_frs_response(bno08x_t *handle)
{
    handle->frsResp = true; // Indicate received response

    if (handle->cmdBuffer[0] == BNO08X_REPORT_FRS_READ_RESPONSE)
    {    
        memcpy(handle->frsBuffer, handle->cmdBuffer, 16);
        return 16;
    }
    else // Otherwise it's BNO08X_REPORT_FRS_WRITE_RESPONSE
    {
        memcpy(handle->frsBuffer, handle->cmdBuffer, 4);
        return 4;
    }

}


/****************************************************************************
 * FRS and Memory
 ****************************************************************************/


/****************************************************************************
 * @brief Handles the entire FRS Write Process - Sends FRS Write Request, processes response, then sends FRS Write Data.
 * @param handle Handle for BNO08x chip.
 * @param record Record to write to (uint16_t)
 * @param data Data to write to record (array of bytes).
 * @param offset 32-bit word offset from start of record to begin writing data.
 * @param words Length of data to write to record in 32-bit words.
 * @return 0: Success
 *  1: Timed out
 *  2: FRS Write Error
 ****************************************************************************/
uint8_t bno08x_FRS_write(bno08x_t *handle, uint16_t record, uint8_t *data, uint16_t offset, uint16_t words)
{
    
    /* FRS Write Process
    1) Send FRS Write Request
    2) Receive FRS Write Response
    3) Send FRS Write Data Request with data to be written
    4) Receive FRS Write Response

    FRS Write Request:
    Byte 0 - Report ID (FRS Write Request)
    Byte 1 - Reserved
    Byte 2 - Length LSB
    Byte 3 - Length MSB
    Byte 4 - FRS Type LSB (Record Address)
    Byte 5 - FRS Type MSB

    FRS Write Response:
    Byte 0 - Report ID (FRS Write Response)
    Byte 1 - Status/Error
    Byte 2 - Word Offset LSB from beginning of record for last written word of data
    Byte 3 - Word OFfset MSB

    FRS Write Data Request:
    Byte 0 - Report ID (FRS Write Data Request)
    Byte 1 - Reserved
    Byte 2 - Offset LSB In 32-bit words from beginning of record where write begins
    Byte 3 - Offset MSB
    Byte 4-7 - Data 0 LSB to MSB
    Byte 8-11 - Data 1 LSB to MSB

    */

    uint16_t wordsLeft = words;

    uint16_t offsetFwd = offset; // Tracks current position of write in record

    uint8_t header[4];

    uint8_t frsReq[6]; // FRS Write Request
    frsReq[0] = BNO08X_REPORT_FRS_WRITE_REQUEST;
    frsReq[1] = 0; // Reserved
    frsReq[2] = (uint8_t)(wordsLeft & 0xFF);
    frsReq[3] = (uint8_t)(wordsLeft >> 8);
    frsReq[4] = (uint8_t)(record & 0xFF);
    frsReq[5] = (uint8_t)(record >> 8);


    uint8_t frsWrite[4]; // FRS Write Request Data

    uint8_t request[10]; // Request Message

    header[0] = sizeof(request); // Length LSB
    header[1] = 0; // Length MSB
    header[2] = BNO08X_CHANNEL_SENSOR_CTRL;
    header[3] = handle->ch2Seq;

    // Fill Request Message
    memcpy(request, header, sizeof(header));

    memcpy(request + sizeof(header), frsReq, sizeof(frsReq));

    memset(handle->frsBuffer, 0x00, sizeof(handle->frsBuffer)); // Clear FRS buffer

    // Timer to wait for FRS to report available
    timer_handle_t frs_timer;
    timer_init(&frs_timer, 1000000); // 1s

    // Timer for individual data send
    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms

    timer_start(&frs_timer);
    timer_start(&gen_timer);

    // Send Write Request, keep trying if busy
    while (handle->frsBuffer[1] != BNO08X_FRS_WRITE_RESP_WRITE_READY)
    {

        if ((handle->frsBuffer[0] == BNO08X_REPORT_FRS_WRITE_RESPONSE) && (handle->frsBuffer[1] != BNO08X_FRS_WRITE_RESP_BUSY))
            return 2; // Error other than busy, don't try again

        if (timer_check_exp(&frs_timer))
            return 1;

        handle->frsResp = false;
            
        timer_reset(&gen_timer);

        while (bno08x_tx(handle, request, sizeof(request)))
        {
            if (timer_check_exp(&gen_timer))
                return 1;

        }

        handle->ch2Seq++; // Increment to next sequence number

        timer_reset(&gen_timer);

        // Wait for response
        while (!handle->frsResp)
        {
            if (timer_check_exp(&gen_timer))
                return 1;

            bno08x_get_messages(handle);
        }

        if (handle->frsBuffer[0] != BNO08X_REPORT_FRS_WRITE_RESPONSE)
            return 2;

    }

    // Greenlight from Sensor to write data

    memset(handle->frsBuffer, 0x00, sizeof(handle->frsBuffer)); // Clear FRS buffer

    // Fill report info
    frsWrite[0] = BNO08X_REPORT_FRS_WRITE_DATA;
    frsWrite[1] = 0; // Reserved
    frsWrite[2] = (uint8_t)(offsetFwd & 0xFF);
    frsWrite[3] = (uint8_t)(offsetFwd >> 8);

    uint8_t write[16];

    // While there's still data left to write
    while (wordsLeft > 0)
    {

        header[3] = handle->ch2Seq;

        timer_blocking_delay(100); // 100us between writes

        // Construct messages, different sizes depending on data left
        if (wordsLeft >= 2)
        {
            header[0] = sizeof(write);

            memcpy(write, header, sizeof(header));

            memcpy(write + sizeof(header), frsWrite, sizeof(frsWrite));

            memcpy(write + sizeof(header) + sizeof(frsWrite), data + (offsetFwd - offset) * 4, 8);
        }
        else if (wordsLeft == 1)
        {
            header[0] = sizeof(write) - 4;

            memcpy(write, header, sizeof(header));

            memcpy(write + sizeof(header), frsWrite, sizeof(frsWrite));

            memcpy(write + sizeof(header) + sizeof(frsWrite), data + (offsetFwd - offset) * 4, 4);
        }

        timer_reset(&frs_timer);

        // Write data and wait for response
        while ((handle->frsBuffer[0] != BNO08X_REPORT_FRS_WRITE_RESPONSE) || (handle->frsBuffer[1] != BNO08X_FRS_WRITE_RESP_WORDS_RX))
        {

            if ((handle->frsBuffer[0] == BNO08X_REPORT_FRS_WRITE_RESPONSE) && (handle->frsBuffer[1] != BNO08X_FRS_WRITE_RESP_BUSY))
                return 2; // Error other than busy, don't try again

            if (timer_check_exp(&frs_timer))
                return 1;

            handle->frsResp = false;
                
            timer_reset(&gen_timer);

            while (bno08x_tx(handle, write, sizeof(write)))
            {
                if (timer_check_exp(&gen_timer))
                    return 1;

            }
            handle->ch2Seq++;

            timer_reset(&gen_timer);

            while (!handle->frsResp)
            {
                if (timer_check_exp(&gen_timer))
                    return 1;

                bno08x_get_messages(handle);
            }

        }

        memset(handle->frsBuffer, 0x00, sizeof(handle->frsBuffer)); // Clear buffer for next write

        // Update info for write
        if (wordsLeft >= 2)
        {
            wordsLeft -= 2;
            offsetFwd += 2;
        }
        else if (wordsLeft == 1)
        {
            wordsLeft--;
            offsetFwd++;
        }

        frsWrite[2] = (uint8_t)(offsetFwd & 0xFF);
        frsWrite[3] = (uint8_t)(offsetFwd >> 8);

    }

    timer_reset(&gen_timer);

    // Writing complete, wait for validated record
    while((handle->frsBuffer[0] != BNO08X_REPORT_FRS_WRITE_RESPONSE) || (handle->frsBuffer[1] != BNO08X_FRS_WRITE_RESP_RECORD_VALID))
    {
        
        if (timer_check_exp(&gen_timer))
            return 1;

        handle->frsResp = false;

        bno08x_get_messages(handle);
    }

    memset(handle->frsBuffer, 0x00, sizeof(handle->frsBuffer));

    timer_reset(&gen_timer);

    // Wait for notification that write record completed
    while((handle->frsBuffer[0] != BNO08X_REPORT_FRS_WRITE_RESPONSE) || (handle->frsBuffer[1] != BNO08X_FRS_WRITE_RESP_COMPLETE))
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        handle->frsResp = false;

        bno08x_get_messages(handle);
    }

    // Allow time for record to write internally, may not be necessary
    //timer_blocking_delay(25000); // 25ms

    return 0;

}

/****************************************************************************
 * @brief Handles the entire FRS Read Process - Sends FRS Read Request, processes response, then read FRS Data.
 * @param handle Handle for BNO08x chip.
 * @param record Record to read from (uint16_t)
 * @param data Data output from record (array of bytes).
 * @return 0: Success
 *  1: Timed out
 *  2: FRS Read Error
 *  3: FRS Busy
 ****************************************************************************/
uint8_t bno08x_FRS_read(bno08x_t *handle, uint16_t record, uint8_t *data)
{
    /* 

    FRS Read Process
    1) Send FRS Read Request
    2) Receive FRS Read Response

    FRS Read Request:
    Byte 0 - Report ID (FRS Read Request)
    Byte 1 - Reserved
    Byte 2 - Reserved
    Byte 3 - Reserved
    Byte 4 - FRS Type LSB (Register Address)
    Byte 5 - FRS Type MSB
    Byte 6 - Reserved
    Byte 7 - Reserved

    FRS Read Response:
    Byte 0 - Report ID (FRS Read Response)
    Byte 1 - 0:3 Status, 4:7 Data Length
    Byte 2 - Word Offset LSB from beginning of record for last written word of data
    Byte 3 - Word OFfset MSB
    Byte 4-7 - Data0 LSB to MSB
    Byte 8-11 - Data1 LSB to MSB
    Byte 12 - FRS Type LSB (Record data belongs to)
    Byte 13 - FRS Type MSB
    Byte 14 - Reserved
    Byte 15 - Reserved

    */

    uint8_t bytesRecd = 0; // Tracker for data read

    uint8_t header[4];

    uint8_t frsReq[6]; // FRS Read Request
    frsReq[0] = BNO08X_REPORT_FRS_READ_REQUEST;
    frsReq[1] = 0; // Reserved
    frsReq[2] = 0; // Reserved
    frsReq[3] = 0; // Reserved
    frsReq[4] = (uint8_t)(record & 0xFF);
    frsReq[5] = (uint8_t)(record >> 8);

    uint8_t request[12]; // Request Message

    header[0] = sizeof(request); // Length LSB
    header[1] = 0; // Length MSB
    header[2] = BNO08X_CHANNEL_SENSOR_CTRL; // Channel 2
    header[3] = handle->ch2Seq;

    // Fill Request Message
    memcpy(request, header, sizeof(header));

    memcpy(request + sizeof(header), frsReq, sizeof(frsReq));

    // Timer to wait for FRS to report available
    timer_handle_t frs_timer;
    timer_init(&frs_timer, 1000000); // 1s

    // Timer for individual data send
    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms

    handle->frsResp = false; // Response notification

    memset(handle->frsBuffer, 0x00, sizeof(handle->frsBuffer)); // Clear FRS Buffer
        
    timer_start(&gen_timer);

    // Send read request
    while (bno08x_tx(handle, request, sizeof(request)))
    {
        if (timer_check_exp(&gen_timer))
            return 1;
    }

    handle->ch2Seq++; // Increment to next sequence number

    timer_start(&frs_timer);

    // Read data until receive complete message
    while ((((handle->frsBuffer[1] & 0x0F) != BNO08X_FRS_READ_RESP_COMPLETE)) && ((handle->frsBuffer[1] & 0x0F) != BNO08x_FRS_READ_DEPRECATED))
    {

        if (timer_check_exp(&frs_timer))
            return 1;

        memset(handle->frsBuffer, 0x00, sizeof(handle->frsBuffer));

        timer_reset(&gen_timer);

        // Wait for response
        while (!handle->frsResp)
        {
            if (timer_check_exp(&gen_timer))
                return 1;

            bno08x_get_messages(handle); // Need to add timer here
        }

        handle->frsResp = false;

        // Handle any errors
        if (handle->frsBuffer[0] != BNO08X_REPORT_FRS_READ_RESPONSE)
            return 2;
        if ((handle->frsBuffer[1] & 0x0F) == BNO08X_FRS_READ_RESP_BUSY)
            return 3;
        if (((handle->frsBuffer[1] & 0x0F) == BNO08X_FRS_READ_RESP_UNRECOG_TYPE) || ((handle->frsBuffer[1] & 0x0F) == BNO08X_FRS_READ_EMPTY) ||
            ((handle->frsBuffer[1] & 0x0F) == BNO08X_FRS_RESP_DEVICE_ERROR))
            return 2;
        
        // Process Data
        if ((handle->frsBuffer[1] & 0xF0) == 0x20) // Two 4 byte words
        {
            for (uint8_t i = 0; i < 8; i++)
            {
                data[bytesRecd] = handle->frsBuffer[4+i];
                bytesRecd++;
            }
        }
        else if ((handle->frsBuffer[1] & 0xF0) == 0x10) // One 4 byte word
        {
            for (uint8_t i = 0; i < 4; i++)
            {
                data[bytesRecd] = handle->frsBuffer[4+i];
                bytesRecd++;
            }
        }

    }

    return 0;

}

/****************************************************************************
 * @brief Request and report errors in BNO08x error queue.
 * @param handle Handle for BNO08x chip.
 * @param severity Severity of errors to report.
 * @param severities Severity of error outputs (array of bytes).
 * @param errors Error outputs (array of bytes).
 * @param sources Error source outputs (array of bytes).
 * @param modules Error module outputs (array of bytes).
 * @param codes Error code outputs (array of bytes).
 * @return 0-X: Success/Number of Errors
 *  -1: Timed out
 ****************************************************************************/
int bno08x_report_errors(bno08x_t *handle, uint8_t severity, uint8_t *severities, uint8_t *errors, uint8_t *sources, uint8_t *modules, uint8_t *codes)
{

    uint8_t i = 0;

    uint8_t params[] = {severity, 0, 0, 0, 0, 0, 0, 0, 0};

    // Timer to receive all errors
    timer_handle_t err_timer;
    timer_init(&err_timer, 1000000); // 1s

    // Timer for individual data send
    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms

    if (bno08x_cmd_request(handle, BNO08X_CMD_ERRORS, params))
        return -1;

    timer_start(&err_timer);

    // Read messages until we've read all errors
    while (!timer_check_exp(&err_timer))
    {

        handle->isErr = false;

        timer_reset(&gen_timer);

        while(!handle->isErr)
        {
            if (timer_check_exp(&gen_timer))
                return 1;

            bno08x_get_messages(handle);
        }

        // Process errors
        if (handle->cmdBuffer[7] != BNO08X_ERR_SOURCE_NO_ERROR)
        {
            errors[i] = handle->cmdBuffer[8];
            sources[i] = handle->cmdBuffer[7];
            modules[i] = handle->cmdBuffer[9];
            codes[i] = handle->cmdBuffer[10];

            i++;
        }
        else if (handle->cmdBuffer[7] == BNO08X_ERR_SOURCE_NO_ERROR)
            return i; // Return number of errors
    
    }

    return -1; // Timed out

}


/****************************************************************************
 * Sensor Report Requests
 ****************************************************************************/


/****************************************************************************
 * @brief Request information about a sensor.
 * @param handle Handle for BNO08x chip.
 * @param sensor Sensor ID
 * @param sensitivity Threshold of sensor output required to be reported (read as int).
 * @param interval Interval in microseconds between reports.
 * @param batch Batch Interval - Max microsecond delay between sensor sample and report.
 * @param configWord 32-bit Word for sensor specific configuration options.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_feature_get(bno08x_t *handle, uint8_t sensor, uint8_t *flags, uint16_t *sensitivity, uint32_t *interval, uint32_t *batch, uint32_t *configWord)
{

    uint8_t params[] = {sensor};

    handle->reports[sensor].respRcd = false;

    // Send request
    if (bno08x_feature_request(handle, BNO08X_REPORT_GET_FEATURE_REQUEST, params))
        return 1;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for response
    while (!handle->reports[sensor].respRcd)
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        bno08x_get_messages(handle);
    }

    *flags = handle->buffer[2];
    *sensitivity = (uint16_t)handle->buffer[7] + ((uint16_t)handle->buffer[8] << 8);
    *interval = (uint32_t)handle->buffer[9] + ((uint32_t)handle->buffer[10] << 8) + ((uint32_t)handle->buffer[11] << 16) + ((uint32_t)handle->buffer[12] << 24);
    *batch = (uint32_t)handle->buffer[13] + ((uint32_t)handle->buffer[14] << 8) + ((uint32_t)handle->buffer[15] << 16) + ((uint32_t)handle->buffer[16] << 24);
    *configWord = (uint32_t)handle->buffer[17] + ((uint32_t)handle->buffer[18] << 8) + ((uint32_t)handle->buffer[19] << 16) + ((uint32_t)handle->buffer[20] << 24);

    return 0;

}

/****************************************************************************
 * @brief Set report configuration, inluding enable.
 * @param handle Handle for BNO08x chip.
 * @param sensor Sensor ID.
 * @param flags Bit 0: Sensitivity Type, Bit 1: Sensitivity Enable, Bit 2: Wake-up Enable, Bit 3: Always-on Enable, All others: reserved.
 * @param sensitivity Threshold of sensor output required to be reported (write as int).
 * @param interval Interval in microseconds between reports.
 * @param batch Batch Interval - Max microsecond delay between sensor sample and report.
 * @param configWord 32-bit Word for sensor specific configuration options.
 * @return 0: Success
 *  1: Timed out
 *  2: BNO08x Response does not match requested
 *  3: Failed to send Set Feature Command
 ****************************************************************************/
uint8_t bno08x_feature_set(bno08x_t *handle, uint8_t sensor, uint8_t flags, uint16_t sensitivity, uint32_t interval, uint32_t batch, uint32_t configWord)
{

    uint8_t params[16];

    params[0] = sensor;
    params[1] = flags;
    params[2] = (uint8_t)(sensitivity & 0xFF);
    params[3] = (uint8_t)(sensitivity >> 8);
    params[4] = (uint8_t)(interval & 0xFF);
    params[5] = (uint8_t)((interval >> 8) & 0xFF);
    params[6] = (uint8_t)((interval >> 16) & 0xFF);
    params[7] = (uint8_t)((interval >> 24) & 0xFF);
    params[8] = (uint8_t)(batch & 0xFF);
    params[9] = (uint8_t)((batch >> 8) & 0xFF);
    params[10] = (uint8_t)((batch >> 16) & 0xFF);
    params[11] = (uint8_t)((batch >> 24) & 0xFF);
    params[12] = (uint8_t)(configWord & 0xFF);
    params[13] = (uint8_t)((configWord >> 8) & 0xFF);
    params[14] = (uint8_t)((configWord >> 16) & 0xFF);
    params[15] = (uint8_t)((configWord >> 24) & 0xFF);

    handle->reports[sensor].respRcd = false;

    // Send Set Feature Command
    if (bno08x_feature_request(handle, BNO08X_REPORT_SET_FEATURE_COMMAND, params))
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Failed to send Set Feature Command");
        #endif
        return 3;
    }

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for response
    while (!handle->reports[sensor].respRcd)
    {
        
        if (timer_check_exp(&gen_timer))
        {
            #ifdef DEBUG
            Serial.println("[DEBUG] Timed out waiting for Set Feature Command Response");
            #endif
            return 1;
        }

        bno08x_get_messages(handle);

    }

    handle->reports[sensor].respRcd = false; // Reset response

    // Confirm response matches settings requested
    if ((flags & 0x01) != handle->reports[sensor].sensType)
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Set Feature Response does not match Sensor Type");
        #endif
        return 2;
    }
    if (((flags >> 1) & 0x01) != handle->reports[sensor].sensEn)
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Set Feature Response does not match Sensor Enable");
        #endif
        return 2;
    }
    if (((flags >> 2) & 0x01) != handle->reports[sensor].wakeupEn)
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Set Feature Response does not match Wakeup Enable");
        #endif
        return 2;
    }
    if (((flags >> 3) & 0x01) != handle->reports[sensor].alwaysonEn)
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Set Feature Response does not match Always on Enable");
        #endif
        return 2;
    }
    if (handle->reports[sensor].enabled != ((interval > 0) || (batch > 0))) // Interval and batch may change from requested based on sensor
    {
        #ifdef DEBUG
        Serial.println("[DEBUG] Set Feature Response does not match interval or batch");
        #endif
        return 2;
    }

    return 0;

}

/****************************************************************************
 * @brief Send Get/Set Feature Request
 * @param handle Handle for BNO08x chip.
 * @param type Set or Get Feature Request Command.
 * @param params Parameters for request - See SH-2 Reference Manual (array of bytes).
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
static uint8_t bno08x_feature_request(bno08x_t *handle, uint8_t type, uint8_t *params)
{

    uint8_t header[4];

    uint8_t req[21];

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    header[1] = 0; // Length MSB
    header[2] = BNO08X_CHANNEL_SENSOR_CTRL;
    header[3] = handle->ch2Seq;

    memcpy(req, header, sizeof(header)); // Prepare request

    req[4] = type;
    req[5] = params[0];

    // Prepare and send specific message for Get/Set Feature Request
    if (type == BNO08X_REPORT_SET_FEATURE_COMMAND)
    {

        req[0] = sizeof(req); // Length LSB

        memcpy(req + 6, params + 1, 15);

        while (bno08x_tx(handle, req, sizeof(req)))
        {
            if (timer_check_exp(&gen_timer))
                return 1;

            bno08x_get_messages(handle);
        }
    }
    else
    {

        req[0] = 6; // Length LSB

        while (bno08x_tx(handle, req, 6))
        {
            if (timer_check_exp(&gen_timer))
                return 1;

            bno08x_get_messages(handle);
        }
    }

    handle->ch2Seq++;

    return 0;

}


/****************************************************************************
 * Sensor and Configuration Information
 ****************************************************************************/


/****************************************************************************
 * @brief Get sensor report count information.
 * @param handle Handle for BNO08x chip.
 * @param sensorID Sensor ID.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_counts_get(bno08x_t *handle, uint8_t sensorID, uint32_t *read, uint32_t *threshold, uint32_t *prod, uint32_t *filter)
{

    uint8_t params[] = {0x00, sensorID, 0, 0, 0, 0, 0, 0, 0};

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms

    handle->rspCount = false; // Reset response flag

    timer_start(&gen_timer);

    // Send Get Counts command
    if (bno08x_cmd_request(handle, BNO08X_CMD_COUNTER, params))
        return 1;

    while (!handle->rspCount)
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        bno08x_get_messages(handle);
    }

    *read = handle->reports[sensorID].countRead; // Total number of Samples Offered
    *threshold = handle->reports[sensorID].thresholdPass; // Number of Samples that Passed Threshold Requirements and were Attempted to be Transmitted
    *prod = handle->reports[sensorID].countProd; // Total Number of Samples Produced
    *filter = handle->reports[sensorID].filterPass; // Number of Samples that Passed Decimation Filter

    return 0;

}

/****************************************************************************
 * @brief Clear information on sensor report counts.
 * @param handle Handle for BNO08x chip.
 * @param sensorID Sensor ID.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_counts_clr(bno08x_t *handle, uint8_t sensorID)
{

    uint8_t params[] = {0x01, sensorID, 0, 0, 0, 0, 0, 0, 0};

    // Send clear counts command
    if (bno08x_cmd_request(handle, BNO08X_CMD_COUNTER, params))
        return 1;

    handle->reports[sensorID].countProd = 0;
    handle->reports[sensorID].countRead = 0;
    handle->reports[sensorID].filterPass = 0;
    handle->reports[sensorID].thresholdPass = 0;

    // Supposedly there's no response

    return 0;

}

/****************************************************************************
 * @brief Get information about oscillator used.
 * @param handle Handle for BNO08x chip.
 * @return 0: Internal Oscillator
 *  1: External Crystal
 *  2: External Clock
 *  -1: Timed out
 ****************************************************************************/
int bno08x_osc_get(bno08x_t *handle)
{

    uint8_t params[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    handle->oscType = 255; // Set to invalid number

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms

    // Send Get Oscillator command
    if (bno08x_cmd_request(handle, BNO08X_CMD_OSC, params))
        return -1;

    timer_start(&gen_timer);

    // Wait to receive response
    while (handle->oscType == 255)
    {
        if (timer_check_exp(&gen_timer))
            return -1;

        bno08x_get_messages(handle);
    }

    return handle->oscType;

}


/****************************************************************************
 * Tare
 ****************************************************************************/


/****************************************************************************
 * @brief Perform Tare Operation.
 * @param handle Handle for BNO08x chip.
 * @param x 1: Tare X axis.
 * @param y 1: Tare Y axis.
 * @param z 1: Tare Z axis.
 * @param rotVector Vector to use for Tare - 0: Rotation Vector, 1: Gaming Rotation Vector, 2: Geomagnetic Rotation Vector,
 * 3: Gyro-Integrated Rotation Vector, 4: ARVR-Stabilized Rotation Vector, 5: ARVR-Stabilized Game Rotation Vector.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_tare(bno08x_t *handle, uint8_t x, uint8_t y, uint8_t z, uint8_t rotVector)
{

    uint8_t params[] = {0x00, (uint8_t)(x + (y << 1) + (z << 2)), rotVector, 0, 0, 0, 0, 0, 0};

    // Send Tare Now Command
    if (bno08x_cmd_request(handle, BNO08X_CMD_TARE, params))
        return 1;

    // No response for tare commands

    return 0;

}

/****************************************************************************
 * @brief Persist results of last Tare operation to flash memory. Only applies to Rotation and Geomagnetic Rotation Vector.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_tare_persist(bno08x_t *handle)
{

    uint8_t params[] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0};

    // Send Tare Persist Command
    if (bno08x_cmd_request(handle, BNO08X_CMD_TARE, params))
        return 1;

    // No response for tare commands

    return 0;

}

/****************************************************************************
 * @brief Set sensor orientation - Does not replace persistent tare settings. Set all to 0 to clear current tare.
 * @param handle Handle for BNO08x chip.
 * @param x Rotation Quaternion (16-bit 2's compliment, Q-point 14)
 * @param y Rotation Quaternion (16-bit 2's compliment, Q-point 14)
 * @param z Rotation Quaternion (16-bit 2's compliment, Q-point 14)
 * @param w Rotation Quaternion (16-bit 2's compliment, Q-point 14)
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_tare_reorient(bno08x_t *handle, int16_t x, int16_t y, int16_t z, int16_t w)
{
    // values should be 16 bit two's compliment with a q-point of 14

    uint8_t params[] = {0x02, (uint8_t)(x & 0xFF), (uint8_t)(x >> 8), (uint8_t)(y & 0xFF), (uint8_t)(y >> 8), (uint8_t)(z & 0xFF), (uint8_t)(z >> 8), (uint8_t)(w & 0xFF), (uint8_t)(w >> 8)};

    // Send Reorient Command
    if (bno08x_cmd_request(handle, BNO08X_CMD_TARE, params))
        return 1;

    // No response for tare commands

    return 0;

}


/****************************************************************************
 * Calibration
 ****************************************************************************/


/****************************************************************************
 * @brief Enable/Disable Motion Engine Calibration Routines.
 * @param handle Handle for BNO08x chip.
 * @param accel 1: Enable Accelerometer Calibration
 * @param gyro 1: Enable Gyro Calibration
 * @param mag 1: Enable Magnetometer Calibration
 * @param planar 1: Enable Planar Accelerometer Calibration
 * @param table 1: Enable On Table Calibration
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_mecal_config(bno08x_t *handle, uint8_t accel, uint8_t gyro, uint8_t mag, uint8_t planar, uint8_t table)
{

    uint8_t params[] = {accel, gyro, mag, 0x00, planar, table, 0, 0, 0};

    handle->calSuccess = false; // Reset response notification

    // Send MECal command with params
    if (bno08x_cmd_request(handle, BNO08X_CMD_MECAL, params))
        return 1;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for response
    while (!handle->calSuccess)
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        bno08x_get_messages(handle);
    }

    return 0;

}

/****************************************************************************
 * @brief Get Current Motion Engine Calibration Routine Settings.
 * @param handle Handle for BNO08x chip.
 * @param accel 1: Enable Acceleration Calibration
 * @param gyro 1: Enable Gyro Calibration
 * @param mag 1: Enable Magnetometer Calibration
 * @param planar 1: Enable Planar Acceleration Calibration
 * @param table 1: Enable On Table Calibration
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_mecal_get(bno08x_t *handle, uint8_t *accel, uint8_t *gyro, uint8_t *mag, uint8_t *planar, uint8_t *table)
{

    uint8_t params[] = {0, 0, 0, 0x01, 0, 0, 0, 0, 0};

    handle->calSuccess = false; // Reset response notification

    // Send MECal command with params
    if (bno08x_cmd_request(handle, BNO08X_CMD_MECAL, params))
        return 1;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for response
    while (!handle->calSuccess)
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        bno08x_get_messages(handle);
    }

    *accel = handle->accelCal;
    *gyro = handle->gyroCal;
    *mag = handle->magCal;
    *planar = handle->planCal;
    *table = handle->tableCal;

    return 0;

}


/****************************************************************************
 * Configuration Storage
 ****************************************************************************/


/****************************************************************************
 * @brief Save the current DCD to FRS a single time. DCD on RAM is updated every 5s.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_dcd_save(bno08x_t *handle)
{

    handle->saveDCD = false;

    uint8_t params[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Send DCD save command
    if (bno08x_cmd_request(handle, BNO08X_CMD_DCD_SAVE, params))
        return 1;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for response
    while (!handle->saveDCD)
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        bno08x_get_messages(handle);
    }

    return 0;

}

/****************************************************************************
 * @brief Configure the BNO08x to save the DCD periodically.
 * @param handle Handle for BNO08x chip.
 * @param enable True: Enable periodic saving.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_dcd_periodic(bno08x_t *handle, bool enable)
{

    uint8_t params[] = {!enable, 0, 0, 0, 0, 0, 0, 0, 0};

    // Send DCD save periodic command
    if (bno08x_cmd_request(handle, BNO08X_CMD_DCD_PERIODIC, params))
        return 1;

    // No response is sent for this command

    return 0;

}

/****************************************************************************
 * @brief Clear DCD data from RAM and send reset command to BNO08x.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
uint8_t bno08x_dcd_clr(bno08x_t *handle)
{
    /*
     * The instructions for this command (6.4.9 of SH-2 Ref Manual) are confusing.
     * The recommended sequence for completely resetting the DCD state is to reset the hub, delete the flash copy of the DCD via FRS, then issue the clear DCD & Reset Cmd
     * However, there is not DCD record in the FRS. Moreover, the Clear DCD & Reset Command is supposed to delete the DCD from the RAM so it doesn't copy to flash
     * on non-power-up reset. So why would I need to do more than just issue this command?
     */

    uint8_t params[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Send command - Cleares DCD stored in RAM and resets chip.
    if (bno08x_cmd_request(handle, BNO08X_CMD_CLR_DCD, params))
        return 1;

    // Run startup sequence again

    if(handle->busType == BNO08X_SPI) // Used to be != BNO08X_UART, but this is likely unnecessary on I2C as well
    {
        if (bno08x_int_wait(handle))
            return 1;
    }
    
    if(bno08x_SHTP_startup(handle))
        return 1;

    if (bno08x_hub_startup(handle))
        return 1;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for execution channel reset complete response
    while (!handle->isRst)
    {
        
        if (timer_check_exp(&gen_timer))
            return 1;
        
        bno08x_get_messages(handle);

    }

    return 0;

}


/****************************************************************************
 * Interactive Calibration (BNO086 Only)
 ****************************************************************************/


/****************************************************************************
 * @brief Start turntable self-calibration. Begin motion once function returns.
 * @param handle Handle for BNO08x chip.
 * @param interval Interval in microseconds calibration will run - Should be the rate the BNO08x is expected to run at after calibration.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
/* Untested
 uint8_t bno08x_cal_start(bno08x_t *handle, uint32_t interval)
{

    uint8_t params[] = {0x00, (uint8_t)(interval & 0xFF), (uint8_t)((interval >> 8) & 0xFF), (uint8_t)((interval >> 16) & 0xFF), (uint8_t)((interval >> 24) & 0xFF), 0, 0, 0, 0};

    handle->startCal = 255; // Assign arbitrary value

    // Send start self-calibration command
    if (bno08x_cmd_request(handle, BNO08X_CMD_CAL, params))
        return 1;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for response
    while (handle->startCal != 0)
    {
        if (timer_check_exp(&gen_timer))
            return 1;

        bno08x_get_messages(handle);
    }

    return 0;

}
*/

/****************************************************************************
 * @brief Send Finish Calibration Request after completing calibration motion. Tell BNO08x to compute calibration data.
 * @param handle Handle for BNO08x chip.
 * @return 0: Success
 *  1: No Gyro Offset
 *  2: No Stationary Detection
 *  3: Rotation Outside of Specification
 *  4: Gyro Offset Outside of Specification
 *  5: Accelerometer Offset Outside of Specification
 *  6: Gyro Gain Outside of Specification
 *  7: Gyro Period Outside of Specification
 *  8: Gyro Sample Drops Outside of Specification
 *  -1: Timed out
 ****************************************************************************/
/* Untested
 int bno08x_cal_finish(bno08x_t *handle)
{

    uint8_t params[] = {0x01, 0, 0, 0, 0, 0, 0, 0};

    handle->startCal = 255;
    handle->calStatus = 255;

    // Send finish self-calibration command
    if (bno08x_cmd_request(handle, BNO08X_CMD_CAL, params))
        return -1;

    timer_handle_t gen_timer;
    timer_init(&gen_timer, 250000); // 250ms
    timer_start(&gen_timer);

    // Wait for response
    while (handle->startCal != 0x01)
    {
        if (timer_check_exp(&gen_timer))
            return -1;

        bno08x_get_messages(handle);
    }

    return handle->calStatus;

}
*/

/****************************************************************************
 * @brief Configure intended motion for interactive calibration (bno08x_cal_start()/bno08x_cal_finish()).
 * @param handle Handle for BNO08x chip.
 * @param intent Intended motion for calibration.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
/* Untested
 uint8_t bno08x_cal_interactive(bno08x_t *handle, uint8_t intent)
{

    uint8_t params[] = {intent, 0, 0, 0, 0, 0, 0, 0, 0};

    // Send command
    if (bno08x_cmd_request(handle, BNO08X_CMD_INTERACTIVE_CAL, params))
        return 1;

    // No response for this command

    return 0;

}
*/

/****************************************************************************
 * @brief Provide sample wheel encoder data for use in dead reckoning.
 * @param handle Handle for BNO08x chip.
 * @param index 0: Left Wheel, 1: Right Wheel.
 * @param timestamp Timestamp in microseconds.
 * @param data Wheel Data.
 * @param type Data Type - 0: Position, 1: Velocity.
 * @return 0: Success
 *  1: Timed out
 ****************************************************************************/
/* Untested
 uint8_t bno08x_wheel_request(bno08x_t *handle, uint8_t index, uint32_t timestamp, uint16_t data, uint8_t type)
{
    
    uint8_t params[] = {index, (uint8_t)(timestamp & 0xFF), (uint8_t)((timestamp >> 8) & 0xFF), (uint8_t)((timestamp >> 16) & 0xFF), (uint8_t)((timestamp >> 24) & 0xFF), 
        (uint8_t)(data & 0xFF), (uint8_t)((data >> 8) & 0xFF), type, 0};

    // Send command
    if (bno08x_cmd_request(handle, BNO08X_CMD_WHEEL, params))
        return 1;

    // No response for this command

    return 0;

}
*/

/****************************************************************************
 * Sensor Report Retrieval
 ****************************************************************************/


/****************************************************************************
 * @brief Retrieve last received Acceleration sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Acceleration in the x axis (Q-point 8, m/s2).
 * @param y Acceleration in the y axis (Q-point 8, m/s2).
 * @param z Acceleration in the z axis (Q-point 8, m/s2).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_accel(bno08x_t *handle, uint8_t *status, int16_t *x, int16_t *y, int16_t *z)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_ACCEL]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Calibrated Gyro sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Gyroscope in the x axis (Q-point 9, rad/s).
 * @param y Gyroscope in the y axis (Q-point 9, rad/s).
 * @param z Gyroscope in the z axis (Q-point 9, rad/s).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_gyro_cal(bno08x_t *handle, uint8_t *status, int16_t *x, int16_t *y, int16_t *z)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_GYRO]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Calibrated Magnetic Field sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Magnetic Field in the x axis (Q-point 4, uT).
 * @param y Magnetic Field in the y axis (Q-point 4, uT).
 * @param z Magnetic Field in the z axis (Q-point 4, uT).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_mag_cal(bno08x_t *handle, uint8_t *status, int16_t *x, int16_t *y, int16_t *z)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_MAG_FIELD]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Linear Acceleration sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Linear Acceleration in the x axis (Q-point 8, m/s2).
 * @param y Linear Acceleration in the y axis (Q-point 8, m/s2).
 * @param z Linear Acceleration in the z axis (Q-point 8, m/s2).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_linear_accel(bno08x_t *handle, uint8_t *status, int16_t *x, int16_t *y, int16_t *z)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_LIN_ACCEL]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Rotation Vector sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param i Unit Quaternion i Component (Q-point 14).
 * @param j Unit Quaternion j Component (Q-point 14).
 * @param k Unit Quaternion k Component (Q-point 14).
 * @param real Unit Quaternion real Component (Q-point 14).
 * @param acc Accuracy Estimate (Q-point 12, rads).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_rot_vector(bno08x_t *handle, uint8_t *status, int16_t *i, int16_t *j, int16_t *k, int16_t *real, int16_t *acc)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_ROT_VECTOR]);

    *i = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *j = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *k = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *real = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);
    *acc = (uint16_t)report->dataBuff[12] | ((uint16_t)report->dataBuff[13] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Game Rotation Vector sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param i Unit Quaternion i Component (Q-point 14).
 * @param j Unit Quaternion j Component (Q-point 14).
 * @param k Unit Quaternion k Component (Q-point 14).
 * @param real Unit Quaternion real Component (Q-point 14).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_game_rot_vector(bno08x_t *handle, uint8_t *status, int16_t *i, int16_t *j, int16_t *k, int16_t *real)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_GAME_ROT_VECTOR]);

    *i = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *j = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *k = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *real = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Geomagnetic Rotation Vector sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param i Unit Quaternion i Component (Q-point 14).
 * @param j Unit Quaternion j Component (Q-point 14).
 * @param k Unit Quaternion k Component (Q-point 14).
 * @param real Unit Quaternion real Component (Q-point 12).
 * @param acc Accuracy Estimate (Q-point 12, rads).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_geo_rot_vector(bno08x_t *handle, uint8_t *status, int16_t *i, int16_t *j, int16_t *k, int16_t *real, int16_t *acc)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_GEOMAG_ROT_VECTOR]);

    *i = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *j = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *k = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *real = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);
    *acc = (uint16_t)report->dataBuff[12] | ((uint16_t)report->dataBuff[13] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Gravity sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Gravity in the x axis (Q-point 8, m/s2).
 * @param y Gravity in the y axis (Q-point 8, m/s2).
 * @param z Gravity in the z axis (Q-point 8, m/s2).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_gravity(bno08x_t *handle, uint8_t *status, int16_t *x, int16_t *y, int16_t *z)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_GRAVITY]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Uncalibrated Gyro sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Gyro in the x axis (Q-point 9, rad/s).
 * @param y Gyro in the y axis (Q-point 9, rad/s).
 * @param z Gyro in the z axis (Q-point 9, rad/s).
 * @param xBias Drift estimate in the x axis (Q-point 9, rad/s).
 * @param yBias Drift estimate in the y axis (Q-point 9, rad/s).
 * @param zBias Drift estimate in the z axis (Q-point 9, rad/s).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_gyro_uncal(bno08x_t *handle, uint8_t *status, int16_t *x, int16_t *y, int16_t *z, int16_t *xBias, int16_t *yBias, int16_t *zBias)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_GYRO_UNCAL]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *xBias = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);
    *yBias = (uint16_t)report->dataBuff[12] | ((uint16_t)report->dataBuff[13] << 8);
    *zBias = (uint16_t)report->dataBuff[14] | ((uint16_t)report->dataBuff[15] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Uncalibrated Magnetic Field sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Magnetic Field in the x axis (Q-point 4, uT).
 * @param y Magnetic Field in the y axis (Q-point 4, uT).
 * @param z Magnetic Field in the z axis (Q-point 4, uT).
 * @param xBias Bias estimate in the x axis (Q-point 4, uT).
 * @param yBias Bias estimate in the y axis (Q-point 4, uT).
 * @param zBias Bias estimate in the z axis (Q-point 4, uT).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_mag_uncal(bno08x_t *handle, uint8_t *status, int16_t *x, int16_t *y, int16_t *z, int16_t *xBias, int16_t *yBias, int16_t *zBias)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_MAG_FIELD_UNCAL]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *xBias = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);
    *yBias = (uint16_t)report->dataBuff[12] | ((uint16_t)report->dataBuff[13] << 8);
    *zBias = (uint16_t)report->dataBuff[14] | ((uint16_t)report->dataBuff[15] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Raw Gyro sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Gyro in the x axis (ADCs).
 * @param y Gyro in the y axis (ADCs).
 * @param z Gyro in the z axis (ADCs).
 * @param temp Gyro temperature.
 * @param stamp Timestamp - Time sample was taken as measured by a hub timer in microseconds.
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_gyro_raw(bno08x_t *handle, uint8_t *status, uint16_t *x, uint16_t *y, uint16_t *z, uint16_t *temp, uint32_t *stamp)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_GYRO_RAW]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *temp = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);
    *stamp = (uint32_t)report->dataBuff[12] | ((uint32_t)report->dataBuff[13] << 8) | ((uint32_t)report->dataBuff[14] << 11) | ((uint32_t)report->dataBuff[15] << 24);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Raw Magnetometer sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Magnetometer in the x axis (ADCs).
 * @param y Magnetometer in the y axis (ADCs).
 * @param z Magnetometer in the z axis (ADCs).
 * @param stamp Timestamp - Time sample was taken as measured by a hub timer in microseconds.
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_mag_raw(bno08x_t *handle, uint8_t *status, uint16_t *x, uint16_t *y, uint16_t *z, uint32_t *stamp)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_MAG_RAW]);

    *x = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *y = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *z = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *stamp = (uint32_t)report->dataBuff[10] | ((uint32_t)report->dataBuff[11] << 8) | ((uint32_t)report->dataBuff[12] << 16) | ((uint32_t)report->dataBuff[13] << 24);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Shake Detector sensor report data from report buffer.
 *  Change sensitivity for Shake Detector should be set to 0.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Shake in the x axis.
 * @param y Shake in the y axis.
 * @param z Shake in the z axis.
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_shake_detect(bno08x_t *handle, uint8_t *status, uint8_t *x, uint8_t *y, uint8_t *z)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_SHAKE_DETECT]);

    *x = report->dataBuff[4] & 0x01;
    *y = (report->dataBuff[4] >> 1) & 0x01;
    *z = (report->dataBuff[4] >> 2) & 0x01;

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Pressure sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param pressure Atmospheric Pressure (Q-point 20, hPa).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_pressure(bno08x_t *handle, uint8_t *status, uint32_t *pressure)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_PRESSURE]);

    *pressure = (uint32_t)report->dataBuff[4] | ((uint32_t)report->dataBuff[5] << 8) | ((uint32_t)report->dataBuff[6] << 16) | ((uint32_t)report->dataBuff[7] << 24);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Ambient Light sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param light Light (Q-point 8, lux).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_light(bno08x_t *handle, uint8_t *status, uint32_t *light)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_AMBIENT_LIGHT]);

    *light = (uint32_t)report->dataBuff[4] | ((uint32_t)report->dataBuff[5] << 8) | ((uint32_t)report->dataBuff[6] << 16) | ((uint32_t)report->dataBuff[7] << 24);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Humidity sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param humidity Humidity (Q-point 8, %).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_humidity(bno08x_t *handle, uint8_t *status, uint16_t *humidity)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_HUMIDITY]);

    *humidity = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Proximity sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param distance Distance (Q-point 4, cm).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_prox(bno08x_t *handle, uint8_t *status, uint16_t *distance)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_PROXIMITY]);

    *distance = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Temperature sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param temp Temperature (Q-point 7, degC).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_temp(bno08x_t *handle, uint8_t *status, int16_t *temp)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_TEMPERATURE]);

    *temp = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Tap Detection sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param x Tap Detected in x direction (positive if tap came from positive direction).
 * @param y Tap Detected in y direction (positive if tap came from positive direction).
 * @param z Tap Detected in z direction (positive if tap came from positive direction).
 * @param dub Tap Detected was a double tap.
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
/* Untested
 uint32_t bno08x_get_tap_detect(bno08x_t *handle, uint8_t *status, uint8_t *x, uint8_t *y, uint8_t *z, uint8_t *dub)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_TAP_DETECTOR]);

    if ((report->dataBuff[4] & 0x01) == 0)
        *x = 0;
    else
        *x = (report->dataBuff[4] & 0x01) | ((report->dataBuff[4] << 6) & 0x80);
    
    if (((report->dataBuff[4] >> 2) & 0x01) == 0)
        *y = 0;
    else
        *y = ((report->dataBuff[4] >> 2) & 0x01) | ((report->dataBuff[4] << 4) & 0x80);

    if ((report->dataBuff[4] >> 4) & 0x01)
        *z = 0;
    else
        *z = ((report->dataBuff[4] >> 4) & 0x01) | ((report->dataBuff[4] << 2) & 0x80);

    *dub = (report->dataBuff[4] >> 6) & 0x01;

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}
*/

/****************************************************************************
 * @brief Retrieve last received Step Detector sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param latency Delay in microseconds from time step occurred until detection.
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_step_detect(bno08x_t *handle, uint8_t *status, uint32_t *latency)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_STEP_DETECT]);

    *latency = (uint32_t)report->dataBuff[4] | ((uint32_t)report->dataBuff[5] << 8) | ((uint32_t)report->dataBuff[6] << 16) | ((uint32_t)report->dataBuff[7] << 24);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Step Count sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param latency Delay in microseconds from time last step occurred until detection.
 * @param steps Number of steps counted - May decrease as previous steps are determined not to be steps.
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_step_count(bno08x_t *handle, uint8_t *status, uint32_t *latency, uint16_t *steps)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_STEP_COUNTER]);

    *latency = (uint32_t)report->dataBuff[4] | ((uint32_t)report->dataBuff[5] << 8) | ((uint32_t)report->dataBuff[6] << 16) | ((uint32_t)report->dataBuff[7] << 24);
    *steps = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Stability Classifier sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param stability State of device - 0: Unknown, 1: On Table, 2: Stationary, 3: Stable, 4: Motion
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_stab_class(bno08x_t *handle, uint8_t *status, uint8_t *stability)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_STAB_CLASS]);

    *stability = report->dataBuff[4];

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Pickup Detector sensor report data from report buffer.
 *  Change sensitivity should be set to 0.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param pickup Pickup detected - 1: Level to not level detected, 2: Stopped within tilt region, 3: Both 1 & 2
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
/* Untested
 uint32_t bno08x_get_pickup_detect(bno08x_t *handle, uint8_t *status, uint8_t *pickup)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_PICKUP_DETECT]);

    *pickup = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}
*/

/****************************************************************************
 * @brief Retrieve last received Stability Detector sensor report data from report buffer.
 *  Change sensitivity should be set to 0.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param stability State of device - 1: Stable State Entered, 2: Stable State Exited
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_stab_detect(bno08x_t *handle, uint8_t *status, uint16_t *stability)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_STAB_DETECT]);

    *stability = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Sleep Detector sensor report data from report buffer.
 *  Change sensitivity should be set to 0.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param state State - 0: Hard Wake, 1: Soft Wake, 2: Light Sleep, 3: Deep Sleep, 4: Unknown
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
/* Untested
 uint32_t bno08x_get_sleep_detect(bno08x_t *handle, uint8_t *status, uint8_t *state)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_SLEEP_DETECT]);

    *state = report->dataBuff[4];

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}
*/

/****************************************************************************
 * @brief Retrieve last received Pocket Detector sensor report data from report buffer.
 *  Change sensitivity should be set to 0.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param state State - 1: Entered in-pocket state, 2: Entered out-of-pocket state
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
/* Untested
 uint32_t bno08x_get_pocket_detect(bno08x_t *handle, uint8_t *status, uint16_t *state)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_POCKET_DETECT]);

    *state = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}
*/

/****************************************************************************
 * @brief Retrieve last received Heart Rate Monitor sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param rate Heart Rate (bpm)
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
/* Untested
 uint32_t bno08x_get_heart_rate(bno08x_t *handle, uint8_t *status, uint16_t *rate)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_HEART_RATE]);

    *rate = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}
*/

/****************************************************************************
 * @brief Retrieve last received ARVR-Stabilized Rotation Vector sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param i Unit Quaternion i Component (Q-point 14).
 * @param j Unit Quaternion j Component (Q-point 14).
 * @param k Unit Quaternion k Component (Q-point 14).
 * @param real Unit Quaternion real Component (Q-point 14).
 * @param acc Accuracy Estimate (Q-point 12, rads).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_arvr_stab_rot_vector(bno08x_t *handle, uint8_t *status, int16_t *i, int16_t *j, int16_t *k, int16_t *real, int16_t *acc)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_ARVR_STAB_ROT_VECTOR]);

    *i = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *j = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *k = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *real = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);
    *acc = (uint16_t)report->dataBuff[12] | ((uint16_t)report->dataBuff[13] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received ARVR-Stabilized Game Rotation Vector sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param i Unit Quaternion i Component (Q-point 14).
 * @param j Unit Quaternion j Component (Q-point 14).
 * @param k Unit Quaternion k Component (Q-point 14).
 * @param real Unit Quaternion real Component (Q-point 14).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_arvr_stab_game_rot_vector(bno08x_t *handle, uint8_t *status, int16_t *i, int16_t *j, int16_t *k, int16_t *real)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_ARVR_STAB_GAME_ROT_VECTOR]);

    *i = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *j = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *k = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *real = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Gyro-Integrated Rotation Vector sensor report data from report buffer.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param i Unit Quaternion i Component (Q-point 14).
 * @param j Unit Quaternion j Component (Q-point 14).
 * @param k Unit Quaternion k Component (Q-point 14).
 * @param real Unit Quaternion real Component (Q-point 14).
 * @param x Angular Velocity x Component (Q-point 10).
 * @param y Angular Velocity y Component (Q-point 10).
 * @param z Angular Velocity z Component (Q-point 10).
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_gyro_integ_rot_vector(bno08x_t *handle, int16_t *i, int16_t *j, int16_t *k, int16_t *real, int16_t *x, int16_t *y, int16_t *z)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_GYRO_INT_ROT_VECTOR]);

    *i = (uint16_t)report->dataBuff[0] | ((uint16_t)report->dataBuff[1] << 8);
    *j = (uint16_t)report->dataBuff[2] | ((uint16_t)report->dataBuff[3] << 8);
    *k = (uint16_t)report->dataBuff[4] | ((uint16_t)report->dataBuff[5] << 8);
    *real = (uint16_t)report->dataBuff[6] | ((uint16_t)report->dataBuff[7] << 8);
    *x = (uint16_t)report->dataBuff[8] | ((uint16_t)report->dataBuff[9] << 8);
    *y = (uint16_t)report->dataBuff[10] | ((uint16_t)report->dataBuff[11] << 8);
    *z = (uint16_t)report->dataBuff[12] | ((uint16_t)report->dataBuff[13] << 8);

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}

/****************************************************************************
 * @brief Retrieve last received Motion Request sensor report data from report buffer.
 *  Change sensitivity should be set to 0.
 *  Return does not include time from report read to time this function was called!
 * @param handle Handle for BNO08x chip.
 * @param status Status of Sensor - 0: Unreliable, 1: Accuracy Low, 2: Accuracy Medium, 3: Accuracy High.
 * @param intent Motion Intent provided in bno08x_cal_interactive()
 * @param request Motion Request to host.
 * @return Time delay from report generation in 100s us (time between report generation and asserted interrupt + time between interrupt assertion and report read).
 ****************************************************************************/
uint32_t bno08x_get_motion_request(bno08x_t *handle, uint8_t *status, uint8_t *intent, uint8_t *request)
{

    bno08x_report_t *report = &(handle->reports[BNO08X_SENSOR_MOTION_REQUEST]);

    *intent = report->dataBuff[4];
    *request = report->dataBuff[5];

    // Only first 2 bits indicate status, rest are MSb of Status Report Delay
    *status = report->dataBuff[2] & 0x03;

    // Reset newData flag
    report->newData = 0;

    // Time delay since interrupt: Timebase delay + Status Report Delay
    return report->timeDelay;

}
