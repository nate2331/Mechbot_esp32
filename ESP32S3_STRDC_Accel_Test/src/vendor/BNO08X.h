/*
 * BNO08x Module H File.
 *
 * @file        BNO08X.h
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

#ifndef BNO08X_H
#define BNO08X_H

#include "i2c_master.h"
#include "spi_master.h"
#include "serial_master.h"
#include "timer.h"
#include "gpio.h"

/*
SHTP protocol

SHTP Header:
Byte 0 - Length LSB, Length indicates total bytes in message (cargo) including header
Byte 1 - Length MSB, last bit (15) indicates if continuation from previous transfer. Length of 0xFFFF is reserved due to liklihood to show up from NACK
Byte 2 - Channel
Byte 3 - SeqNum, Each channel and direction has its own sequence number

After the header, all messages are followed with a report number (i.e. Command, response, etc...)

Channels:
0 - SHTP Command Channel
1 - Executable
2 - Sensor Hub Control (Configuration of Sensor)
-- Outputs from sensors --
3 - Input Sensor Reports (non-wake, not gyroRV) (Unidirectional Sensor Reports from BNO08X)
4 - Wake Input Sensor Reports (for sensors configured as wake sensors)
5 - Gyro Rotation Vector

Command Channel:
Write
Byte 0 - Command byte
Bytes 1-n - Parameters for command

Read
Byte 0 - Response byte
Bytes 1-n - Parameters for response

Executable Channel:
For resetting and obtaining details
Write:
Byte 0 -
    0 - Reserved
    1 - Reset
    2 - On (use to wake from sleep)
    3 - Sleep
    4-255 - Reserved

Read:
Byte 0 -
    0 - Reserved
    1 - Reset Complete
    2-255 - Reserved


I2C:
Does not allow for repeated starts

To enable a sensor (and receive sensor reports), use the Set Feature command (channel 2) to enable a sensor with necessary configuration.
Then read report from appropriate channel (3-5).

Reads that terminate after the length field indicate the user doesn't know how many bytes to read. Subsequent reads will begin by sending unread portions of the cargo
(but always a SHTP header) with the header MSB set. Changing the channel or sending a header with a cleared MSB will lose the cargo.

*/

#define BNO08X_BUFFER_SIZE 1024

// I2C Address Options
#define BNO08X_I2C_A 0x4A
#define BNO08X_I2C_B 0x4B

// Channels
#define BNO08X_CHANNEL_SHTP_CMD 0x00
#define BNO08X_CHANNEL_EXECUTABLE 0x01
#define BNO08X_CHANNEL_SENSOR_CTRL 0x02
#define BNO08X_CHANNEL_INPUT_SENSOR 0x03
#define BNO08X_CHANNEL_WAKE_SENSOR 0x04
#define BNO08X_CHANNEL_GYRO 0x05

// Executable Commands
#define BNO08X_EXEC_CMD_WAKE 2
#define BNO08X_EXEC_CMD_SLEEP 3

// BNO08X Reports (Channel 2)
#define BNO08X_REPORT_GET_FEATURE_REQUEST  0xFE
#define BNO08X_REPORT_SET_FEATURE_COMMAND  0xFD
#define BNO08X_REPORT_GET_FEATURE_RESPONSE 0xFC
#define BNO08x_REPORT_TIMEBASE_REFERENCE   0xFB
#define BNO08x_REPORT_TIMESTAMP_REBASE     0xFA
#define BNO08X_REPORT_PRODUCT_ID_REQUEST   0xF9
#define BNO08X_REPORT_PRODUCT_ID_RESPONSE  0xF8
#define BNO08X_REPORT_FRS_WRITE_REQUEST    0xF7
#define BNO08X_REPORT_FRS_WRITE_DATA       0xF6
#define BNO08X_REPORT_FRS_WRITE_RESPONSE   0xF5
#define BNO08X_REPORT_FRS_READ_REQUEST     0xF4
#define BNO08X_REPORT_FRS_READ_RESPONSE    0xF3
#define BNO08X_REPORT_COMMAND_REQUEST      0xF2 // Yes, these are used in Channel 2, not Channel 0
#define BNO08X_REPORT_COMMAND_RESPONSE     0xF1
#define BNO08X_REPORT_FORCE_FLUSH          0xF0
#define BNO08X_REPORT_FLUSH_COMPLETE       0xEF

// BNO08X Commands (Use with command request/response on channel 2)
#define BNO08X_CMD_ERRORS 1
#define BNO08X_CMD_COUNTER 2
#define BNO08X_CMD_TARE 3
#define BNO08X_CMD_INIT 4
#define BNO08X_CMD_DCD_SAVE 6
#define BNO08X_CMD_MECAL 7
#define BNO08X_CMD_DCD_PERIODIC 9
#define BNO08X_CMD_OSC 0x0A
#define BNO08X_CMD_CLR_DCD 0x0B
#define BNO08X_CMD_CAL 0x0C
#define BNO08X_CMD_BOOTLOADER 0x0D
#define BNO08X_CMD_INTERACTIVE_CAL 0x0E
#define BNO08X_CMD_WHEEL 0x0F

// BNO08x Error Sources
#define BNO08X_ERR_SOURCE_ME 1
#define BNO08X_ERR_SOURCE_MOTION_HUB 2
#define BNO08X_ERR_SOURCE_SENSOR_HUB 3
#define BNO08X_ERR_SOURCE_CHIP_LEVEL 4
#define BNO08X_ERR_SOURCE_NO_ERROR 255

// BNO08X Oscillator Types
#define BNO08X_OSC_INTERNAL 0
#define BNO08X_OSC_EXT_CRYSTAL 1
#define BNO08X_OSC_EXT_CLOCK 2

// BNO08X Interactive Calibration Motion Intents
#define BNO08X_MOTION_INTENT_UNKNOWN 0
#define BNO08X_MOTION_INTENT_STATIONARY_NO_VIBE 1
#define BNO08X_MOTION_INTENT_STATIONARY_VIBE 2
#define BNO08X_MOTION_INTENT_IN_MOTION 3
#define BNO08X_MOTION_INTENT_IN_MOTION_ACCEL 4

// BNO08X Meta Data
#define BNO08X_META_ACCEL 0xE302
#define BNO08X_META_GYRO 0xE306
#define BNO08X_META_MAG_FIELD 0xE309
#define BNO08X_META_LIN_ACCEL 0xE303
#define BNO08X_META_ROT_VECTOR 0xE30B
#define BNO08X_META_GRAVITY 0xE304
#define BNO08X_META_GYRO_UNCAL 0xE307
#define BNO08X_META_GAME_ROT_VECTOR 0xE30C
#define BNO08X_META_GEOMAG_ROT_VECTOR 0xE30D
#define BNO08X_META_PRESSURE 0xE30E
#define BNO08X_META_AMBIENT_LIGHT 0xE30F
#define BNO08X_META_HUMIDITY 0xE310
#define BNO08X_META_PROXIMITY 0xE311
#define BNO08X_META_TEMPERATURE 0xE312
#define BNO08X_META_MAG_FIELD_UNCAL 0xE30A
#define BNO08X_META_TAP_DETECTOR 0xE313
#define BNO08X_META_STEP_COUNTER 0xE315
#define BNO08X_META_SIGNIFICATION_MOTION 0xE316
#define BNO08X_META_STAB_CLASS 0xE317
#define BNO08X_META_ACCEL_RAW 0xE301
#define BNO08X_META_GYRO_RAW 0xE305
#define BNO08X_META_MAG_RAW 0xE308
#define BNO08X_META_STEP_DETECT 0xE314
#define BNO08X_META_SHAKE_DETECT 0xE318
#define BNO08X_META_FLIP_DETECT 0xE319
#define BNO08X_META_PICKUP_DETECT 0xE31A
#define BNO08X_META_STAB_DETECT 0xE31B
#define BNO08X_META_PERSONAL_ACTIVITY_CLASS 0xE31C
#define BNO08X_META_SLEEP_DETECT 0xE31D
#define BNO08X_META_TILT_DETECT 0xE31E
#define BNO08X_META_POCKET_DETECT 0xE31F
#define BNO08X_META_CIRCLE_DETECT 0xE320
#define BNO08X_META_HEART_RATE 0xE321
#define BNO08X_META_ARVR_STAB_ROT_VECTOR 0xE322
#define BNO08X_META_ARVR_STAB_GAME_ROT_VECTOR 0xE323
#define BNO08X_META_GYRO_INT_ROT_VECTOR 0xE324
#define BNO08X_META_MOTION_REQUEST 0xE325

// BNO08X Sensor Reports
#define BNO08X_SENSOR_ACCEL 0x01
#define BNO08X_SENSOR_GYRO 0x02
#define BNO08X_SENSOR_MAG_FIELD 0x03
#define BNO08X_SENSOR_LIN_ACCEL 0x04
#define BNO08X_SENSOR_ROT_VECTOR 0x05
#define BNO08X_SENSOR_GRAVITY 0x06
#define BNO08X_SENSOR_GYRO_UNCAL 0x07
#define BNO08X_SENSOR_GAME_ROT_VECTOR 0x08
#define BNO08X_SENSOR_GEOMAG_ROT_VECTOR 0x09
#define BNO08X_SENSOR_PRESSURE 0x0A
#define BNO08X_SENSOR_AMBIENT_LIGHT 0x0B
#define BNO08X_SENSOR_HUMIDITY 0x0C
#define BNO08X_SENSOR_PROXIMITY 0x0D
#define BNO08X_SENSOR_TEMPERATURE 0x0E
#define BNO08X_SENSOR_MAG_FIELD_UNCAL 0x0F
//#define BNO08X_SENSOR_TAP_DETECTOR 0x10 // Untested
#define BNO08X_SENSOR_STEP_COUNTER 0x11
#define BNO08X_SENSOR_SIGNIFICATION_MOTION 0x12
#define BNO08X_SENSOR_STAB_CLASS 0x13
#define BNO08X_SENSOR_ACCEL_RAW 0x14
#define BNO08X_SENSOR_GYRO_RAW 0x15
#define BNO08X_SENSOR_MAG_RAW 0x16
#define BNO08X_SENSOR_SAR 0x17
#define BNO08X_SENSOR_STEP_DETECT 0x18
#define BNO08X_SENSOR_SHAKE_DETECT 0x19
#define BNO08X_SENSOR_FLIP_DETECT 0x1A
//#define BNO08X_SENSOR_PICKUP_DETECT 0x1B // Untested
#define BNO08X_SENSOR_STAB_DETECT 0x1C
//#define BNO08X_SENSOR_PERSONAL_ACTIVITY_CLASS 0x1E // Untested
//#define BNO08X_SENSOR_SLEEP_DETECT 0x1F // Untested
//#define BNO08X_SENSOR_TILT_DETECT 0x20 // Untested
//#define BNO08X_SENSOR_POCKET_DETECT 0x21 // Untested
//#define BNO08X_SENSOR_CIRCLE_DETECT 0x22 // Untested
//#define BNO08X_SENSOR_HEART_RATE 0x23 // Untested
#define BNO08X_SENSOR_ARVR_STAB_ROT_VECTOR 0x28
#define BNO08X_SENSOR_ARVR_STAB_GAME_ROT_VECTOR 0x29
#define BNO08X_SENSOR_GYRO_INT_ROT_VECTOR 0x2A
#define BNO08X_SENSOR_MOTION_REQUEST 0x2B
//#define BNO08X_SENSOR_OPTICAL_FLOW 0x2C // Untested
//#define BNO08X_SENSOR_DEAD_RECK_POSE 0x2D // Untested

// BNO08X FRS Records
#define BNO08X_FRS_REC_STATIC_CAL_AGM 0x7979
#define BNO08X_FRS_REC_NOMINAL_CAL 0x4D4D
#define BNO08X_FRS_REC_STATIC_CAL_SRA 0x8A8A
#define BNO08X_FRS_REC_NOMINAL_CAL_SRA 0x4E4E
#define BNO08X_FRS_REC_DYNAMIC_CAL 0x1F1F
#define BNO08X_FRS_REC_ME_POWER_MANAGEMENT 0xD3E2
#define BNO08X_FRS_REC_SYS_ORIENT 0x2D3E
#define BNO08X_FRS_REC_PRIM_ACCEL_ORIENT 0x2D41
#define BNO08X_FRS_REC_GYRO_ORIENT 0x2D46
#define BNO08X_FRS_REC_MAG_ORIENT 0x2D4C
#define BNO08X_FRS_REC_ARVR_STAB_ROT 0x3E2D
#define BNO08X_FRS_REC_AVVR_STAB_GAME 0x3E2E
#define BNO08X_FRS_REC_SIG_MOTION_DETECT 0xC274
#define BNO08X_FRS_REC_SHAKE_DETECT 0x7D7D
#define BNO08X_FRS_REC_MAX_FUSION_PERIOD 0xD7D7
#define BNO08X_FRS_REC_SERIAL_NUMBER 0x4B4B
#define BNO08X_FRS_REC_ENV_PRESSURE_CAL 0x39AF
#define BNO08X_FRS_REC_ENV_TEMP_CAL 0x4D20
#define BNO08X_FRS_REC_ENV_HUMIDITY_CAL 0x1AC9
#define BNO08X_FRS_REC_ENV_LIGHT_CAL 0x39B1
#define BNO08X_FRS_REC_ENV_PROX_CAL 0x44DA2
#define BNO08X_FRS_REC_ALS_CAL 0xD401
#define BNO08X_FRS_REC_PROX_CAL 0xD402
#define BNO08X_FRS_REC_STAB_DETECT_CONFIG 0xED85
#define BNO08X_FRS_REC_USER_RECORD 0x74B4
#define BNO08X_FRS_REC_ME_TIME_SOURCE_SEL 0xD403
#define BNO08X_FRS_REC_GYRO_ROT_CONFIG 0xA1A2

// FRS Status/Error
#define BNO08X_FRS_WRITE_RESP_WORDS_RX 0 // Word(s) Received
#define BNO08X_FRS_WRITE_RESP_UNRECOG_TYPE 1 // Unrecognized FRS type
#define BNO08X_FRS_WRITE_RESP_BUSY 2 // Busy
#define BNO08X_FRS_WRITE_RESP_COMPLETE 3 // Write Complete
#define BNO08X_FRS_WRITE_RESP_WRITE_READY 4 // Write Mode Entered or Ready
#define BNO08X_FRS_WRITE_RESP_FAILED 5 // Write Failed
#define BNO08X_FRS_WRITE_RESP_RX_NO_WRITE_MODE 6 // Data received while not in write mode
#define BNO08X_FRS_WRITE_RESP_INVALID_LENG 7 // Invalid Length
#define BNO08X_FRS_WRITE_RESP_RECORD_VALID 8 // Record Valid (Record passed validation)
#define BNO08X_FRS_WRITE_RESP_RECORD_INVALID 9 // Record Invalid (Record failed validation)
#define BNO08X_FRS_WRITE_RESP_DEVICE_ERROR 10 // Device Error (DFU flash unavailable - deprecated)
#define BNO08X_FRS_WRITE_RESP_READ_ONLY 11 // Record is Read Only
#define BNO08X_FRS_WRITE_RESP_MEM_FULL 12 // Unable to Write Record. FRS Memory is Full

// FRS Read Status/Error
#define BNO08X_FRS_READ_RESP_GOOD 0 // No Error
#define BNO08X_FRS_READ_RESP_UNRECOG_TYPE 1 // Unrecognized FRS type
#define BNO08X_FRS_READ_RESP_BUSY 2 // Busy
#define BNO08X_FRS_READ_RESP_COMPLETE 3 // Read Complete
#define BNO08X_FRS_READ_EMPTY 5 // Record Empty
#define BNO08x_FRS_READ_DEPRECATED 7 // Deprecated per datasheet but still appears during Read Complete
#define BNO08X_FRS_RESP_DEVICE_ERROR 8 // Device Error (DFU flash unavailable)

// Sensor Status
#define BNO08X_SENSOR_STATUS_UNRELIABLE 0
#define BNO08X_SENSOR_STATUS_ACC_LOW 1
#define BNO08X_SENSOR_STATUS_ACC_MEDIUM 2
#define BNO08X_SENSOR_STATUS_ACC_HIGH 3

// Bus Types
#define BNO08X_I2C 0
#define BNO08X_SPI 1
#define BNO08X_UART 2

// Sensor Length Map
extern uint8_t report_length[];

/*
Organized so user can call sensor from a particular chip.
They can then assign the pointer to a variable then utilize that sensor when needed.
*/

typedef struct {

    uint8_t id; // Identifies Sensor ID
    uint8_t enabled; // Tracks Sensor Configuration enabled
    uint8_t sensType; // Tracks Sensor Configuration Sensitivity Type
    uint8_t wakeupEn; // Tracks Sensor Configuration Wakeup
    uint8_t sensEn; // Tracks Sensor Configuration Sensitivity Enabled
    uint8_t alwaysonEn; // Tracks Sensor Configuration Always-on
    uint8_t respRcd; // Notification handler received feature response

    bool newData; // Notification handler received new data
    uint8_t dataBuff[BNO08X_BUFFER_SIZE]; // Last Sensor Report Storage Buffer
    int timeDelay; // Time Delay from sensor report generation

    // Counts must be generated by the bno08x_counts_get()
    uint32_t countProd; // Total Number of Samples Produced
    uint32_t countRead; // Total number of Samples Offered
    uint32_t filterPass; // Number of Samples that Passed Decimation Filter
    uint32_t thresholdPass; // Number of Samples that Passed Threshold Requirements and were Attempted to be Transmitted

} bno08x_report_t; // Sensor Report Data and Configuration Storage

typedef struct {

    uint32_t guid;
    bool wakeChan; // Is Wake up Channel
    char appName[15];
    char chanName[15];
} bno08x_chan_t; // Information on BNO08x Channels

typedef struct {

    uint16_t maxRead; // Maximum length for cargo read (including header)
    uint16_t maxWrite; // Maximum length for cargo write (including header)
    uint16_t maxTransferWrite; // Maximum length for a write transfer
    uint16_t maxTransferRead; // Maximum length for a read transfer
    char shtpVer[10]; // SHTP Protocol Version
    uint32_t shtpUartTO; // Time in milliseconds after sending a BSN hub will remain active (UART only)
    bno08x_chan_t chan[6];

    uint8_t rstCause; // Reset Cause
    uint8_t swMaj; // Software Version Major
    uint8_t swMin; // Software Version Minor
    uint32_t swPN; // Software Part Number
    uint32_t swBN; // Software Build Number
    uint16_t swPatch; // Software Version Patch

} bno08x_meta_data_t; // Configuration Information for BNO08x - Reported from SHTP Advert

typedef struct {

    void *bus; // Pointer to communication peripheral bus
    uint8_t busType; // Bus type (e.g. BNO08X_I2C, BNO08X_SPI, BNO08X_UART)
    uint8_t busAddr; // Address for I2C, SS for SPI, Not used for UART
    uint8_t wakePin; // PS0/Wake Pin
    uint8_t pinInt; // Interrupt Pin
    uint8_t pinRst; // Reset Pin

    uint8_t ch1Seq; // Channel 1 Tx Sequence
    uint8_t ch2Seq; // Channel 2 Tx Sequence

    uint8_t cmdSeq; // Channel 2 Tx Command Sequence

    uint8_t buffer[BNO08X_BUFFER_SIZE]; // General Rx Buffer
    uint8_t cmdBuffer[BNO08X_BUFFER_SIZE]; // Buffer for use in commands
    uint8_t frsBuffer[16]; // Buffer for use in FRS functions

    bool isRst; // Identify reset condition of Sensor Hub
    bool isSleep; // Identify sleep condition of Sensor Hub
    bool isInit; // Notification that Sensor Hub was initialized
    bool isID; // Notification that Product ID was received
    bool advertRcd; // Notification that SHTP startup Advertisement was received
    bool calSuccess; // Notification that ME Calibration command was successful
    bool saveDCD; // Notification that DCD Save was successful
    bool rspCount; // Notification that Count response was received
    bool flushComplete; // Notification that flush is complete
    bool frsResp; // Notification that an FRS Response was received
    bool isErr; // Notification that an Report Error Command Response was received
    bool bsnResp; // Notification that a Buffer Status Notification was received (UART)

    uint16_t writeAvail; // Bytes available for write - received from BSN (UART)

    uint8_t startCal; // Notification that interactive calibration has started
    uint8_t calStatus; // Status of interactive calibration

    uint8_t oscType; // Oscillator type

    uint8_t accelCal; // Accelerometer Calibration Routine Configuration
    uint8_t gyroCal; // Gyro Calibration Routine Configuration
    uint8_t magCal; // Magnetometer Calibration Routine Configuration
    uint8_t planCal; // Planar Accelerometer Calibration Routine Configuration
    uint8_t tableCal; // On Table Calibration Routine Configuration

    bno08x_report_t reports[46]; // Sensor Reports
    bno08x_meta_data_t metaData; // BNO08x Meta Data

} bno08x_t; // Handler for BNO08x


// Utility Functions
void bno08x_hw_reset(bno08x_t *);

// Initialization
uint8_t bno08x_init(bno08x_t *);
//uint8_t bno08x_hub_reinit(bno08x_t *);
uint8_t bno08x_sleep(bno08x_t *);
uint8_t bno08x_wake(bno08x_t *);

// Messaging
void bno08x_get_messages(bno08x_t *);
uint8_t bno08x_flush(bno08x_t *, uint8_t);

// FRS and Memory
uint8_t bno08x_FRS_write(bno08x_t *, uint16_t, uint8_t *, uint16_t, uint16_t);
uint8_t bno08x_FRS_read(bno08x_t *, uint16_t, uint8_t *);
int bno08x_report_errors(bno08x_t *, uint8_t, uint8_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *);

// Feature functions
uint8_t bno08x_feature_get(bno08x_t *, uint8_t, uint8_t *, uint16_t *, uint32_t *, uint32_t *, uint32_t *);
uint8_t bno08x_feature_set(bno08x_t *, uint8_t, uint8_t, uint16_t, uint32_t, uint32_t, uint32_t);

// Sensor Report & BNO08x Information
uint8_t bno08x_counts_get(bno08x_t *, uint8_t, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
uint8_t bno08x_counts_clr(bno08x_t *, uint8_t);
int bno08x_osc_get(bno08x_t *);

// Tare Functions
uint8_t bno08x_tare(bno08x_t *, uint8_t, uint8_t, uint8_t, uint8_t);
uint8_t bno08x_tare_persist(bno08x_t *);
uint8_t bno08x_tare_reorient(bno08x_t *, int16_t, int16_t, int16_t, int16_t);

// Calibration
uint8_t bno08x_mecal_config(bno08x_t *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
uint8_t bno08x_mecal_get(bno08x_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *);

// DCD
uint8_t bno08x_dcd_save(bno08x_t *);
uint8_t bno08x_dcd_periodic(bno08x_t *, bool);
uint8_t bno08x_dcd_clr(bno08x_t *);

// Interactive Calibration
//uint8_t bno08x_cal_start(bno08x_t *, uint32_t); // Untested
//int bno08x_cal_finish(bno08x_t *); // Untested
//uint8_t bno08x_cal_interactive(bno08x_t *, uint8_t); // Untested
//uint8_t bno08x_wheel_request(bno08x_t *, uint8_t, uint32_t, uint16_t, uint8_t); // Untested

// Report interfaces
uint32_t bno08x_get_accel(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_gyro_cal(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_mag_cal(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_linear_accel(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_rot_vector(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_game_rot_vector(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_geo_rot_vector(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_gravity(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_gyro_uncal(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_mag_uncal(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_gyro_raw(bno08x_t *, uint8_t *, uint16_t *, uint16_t *, uint16_t *, uint16_t *, uint32_t *);
uint32_t bno08x_get_mag_raw(bno08x_t *, uint8_t *, uint16_t *, uint16_t *, uint16_t *, uint32_t *);
uint32_t bno08x_get_shake_detect(bno08x_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *);
uint32_t bno08x_get_pressure(bno08x_t *, uint8_t *, uint32_t *);
uint32_t bno08x_get_light(bno08x_t *, uint8_t *, uint32_t *);
uint32_t bno08x_get_humidity(bno08x_t *, uint8_t *, uint16_t *);
uint32_t bno08x_get_prox(bno08x_t *, uint8_t *, uint16_t *);
uint32_t bno08x_get_temp(bno08x_t *, uint8_t *, int16_t *);
//uint32_t bno08x_get_tap_detect(bno08x_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *); // Untested
uint32_t bno08x_get_step_detect(bno08x_t *, uint8_t *, uint32_t *);
uint32_t bno08x_get_step_count(bno08x_t *, uint8_t *, uint32_t *, uint16_t *);
uint32_t bno08x_get_stab_class(bno08x_t *, uint8_t *, uint8_t *);
//uint32_t bno08x_get_pickup_detect(bno08x_t *, uint8_t *, uint8_t *); // Untested
uint32_t bno08x_get_stab_detect(bno08x_t *, uint8_t *, uint16_t *);
//uint32_t bno08x_get_sleep_detect(bno08x_t *, uint8_t *, uint8_t *); // Untested
//uint32_t bno08x_get_pocket_detect(bno08x_t *, uint8_t *, uint16_t *); // Untested
//uint32_t bno08x_get_heart_rate(bno08x_t *, uint8_t *, uint16_t *); // Untested
uint32_t bno08x_get_arvr_stab_rot_vector(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_arvr_stab_game_rot_vector(bno08x_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_gyro_integ_rot_vector(bno08x_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *, int16_t *);
uint32_t bno08x_get_motion_request(bno08x_t *, uint8_t *, uint8_t *, uint8_t *);

#endif