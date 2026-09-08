/*
 * BNO08x Accelerometer Example Sketch.
 *
 * @file        BNO08x_Accelerometer.ino
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

/*
 * Many thanks to JP Schramel and his BNO080 Rock Bottom Code for Arduino Atmega 328P 16Mhz (Arduino Nano etc) and Cortex M0 (Simblee)
 * It provided a solid foundation and reference for this effort. Find their work here: https://github.com/jps2000/BNO080/tree/master
 *
 * Additional thanks to Adafruit and Bryan Siepert for their implementation of CEVA's BN008x HAL which also provided a great reference.
 * Find that work here: https://github.com/adafruit/Adafruit_BNO08x/tree/master?tab=readme-ov-file
 */

/*************************************
 * INCLUDES
 *************************************/

#include "interrupt.h"
#include "BNO08X.h"

/*************************************
 * DEFINITIONS
 *************************************/

bno08x_t bno;

// Select desired peripheral, initialization, pinout, interrupt will be configured automatically
//#define USE_I2C
#define USE_SPI
//#define USE_UART

// Select desired data gathering behavior
#define USE_INT
//#define USE_POLL

// Pin Definition
#define LED 3

#ifdef USE_SPI
// Pin definitions for SPI
#define BNO_PIN_INT 14
#define BNO_PIN_RST 20
#define BNO_PIN_WAKE 7
#endif

#ifdef USE_I2C
// Pin definitions for I2C
#define BNO_PIN_INT 17
#define BNO_PIN_RST 16
#define BNO_PIN_WAKE 15
#endif

#ifdef USE_UART
// Pin definitions for UART
#define BNO_PIN_INT 17
#define BNO_PIN_RST 16
#define BNO_PIN_WAKE 6
#define UART_BUFFER_EXTRA // Unnecessary for board configurations with excess of 300 byte read buffers and other communication configurations
#endif

// BNO08x Info
#define BNO_ADDRESS 0x4A // If DI is LOW
//#define BNO_ADDRESS 0x4B // If DI is HIGH
#define BNO_SS 0

// Sensor to enable
const uint8_t report = BNO08X_SENSOR_ACCEL;

// Create struct to keep data organized
typedef struct {
  float x;
  float y;
  float z;
  uint8_t status;
  uint32_t timestamp;
} bno_data_t;

bno_data_t bno_data;

// Create interrupt handler for easier enable/disable
isr_handle_t* bno_int;

// Poll variable changed in INT
volatile uint8_t bno_msg;

int print_interval = 10000; // Print interval in us
int poll_interval = 200000; // In us

const int reporting_frequency = 400; // Reporting frequency in Hz

uint32_t rate = 1000000 / reporting_frequency; 

// Utilities
#define QP(n) (1.0f / (1 << n)) // 1 << n ==  2^-n

// Helpers
timer_handle_t loop_timer; // Create timer to print data at a steady rate
timer_handle_t poll_timer; // Create timer to read messages regularly

// ISR to notify program to read messages
void bno_ISR() {
  bno_msg = 1;

  // Disable interrupt until we read the message
  interrupt_disable(bno_int);
}

// Format data in handler to printable version
void get_data(bno08x_t * ic, bno_data_t * bno_data)
{
  int16_t x, y, z;

  bno_data->timestamp = bno08x_get_accel(ic, &(bno_data->status), &x, &y, &z); // Timestamp in 100s us since interrupt

  bno_data->x = x * QP(8); bno_data->y = y * QP(8); bno_data->z = z * QP(8); // Convert from Q-point and assign to data structure

}

void setup() {
  
  #ifdef USE_SPI
  // SPI Configuration
  bno.bus = &SPI_0;
  bno.busType = BNO08X_SPI;
  bno.busAddr = BNO_SS;

  spi_open((spi_handle_t*)bno.bus, 1000000, SPI_MODE_3, SPI_BIT_ORDER_MSB);

  #endif
  
  #ifdef USE_I2C
  // I2C Configuration
  bno.bus = &i2c1;
  bno.busType = BNO08X_I2C;
  bno.busAddr = BNO_ADDRESS;
  
  i2c_open((i2c_handle_t*)bno.bus, 400000);

  #endif
  
  #ifdef USE_UART
  // UART Configuration
  bno.bus = &uart2;
  bno.busType = BNO08X_UART;

  if(serial_open((serial_handle_t*)bno.bus, 3000000, UART_TYPE_BASIC)) // UART must be 3Mbaud (3000000)
  {
    Serial.println("Failed to open Serial connection")
    while (1)
      ;
  }

  #endif

  // Shared Pin Configuration
  bno.wakePin = BNO_PIN_WAKE;
  bno.pinInt = BNO_PIN_INT;
  bno.pinRst = BNO_PIN_RST;
  
  // Initialize GPIO not handled in BNO handler
  gpio_mode(LED, OUTPUT);
  gpio_write(LED, HIGH);

  timer_init(&loop_timer, print_interval); // Initialize timer
  #ifdef USE_POLL
  timer_init(&poll_timer, poll_interval); // Initialize timer
  #endif

  Serial.begin(115200); // 115200 baud
  while (!Serial) delay(10);

  Serial.println("***************************************");
  Serial.println(" Stardust Orbital BNO08x Accelerometer Example ");
  Serial.println("***************************************");

  // Initialize BNO chip
  uint8_t init = 1;

  while (init)
  {
    init = bno08x_init(&bno);
    switch(init)
    {
      case 0:
        Serial.println("BNO Initialized Successfully");
        break;
      case 1:
        Serial.println("Failed waiting for interrupt");
        break;
      case 2:
        Serial.println("Failed to Find I2C Device");
        delay(1000);
        bno08x_hw_reset(&bno);
        break;
      case 3:
        Serial.println("Failed to Receive Startup Advertisement");
        delay(1000);
        bno08x_hw_reset(&bno);
        break;
      case 4:
        Serial.println("Failed to Receive Hub Startup Message");
        delay(1000);
        bno08x_hw_reset(&bno);
        break;
      case 5:
        Serial.println("Failed to Get Product ID");
        delay(1000);
        bno08x_hw_reset(&bno);
        break;
      case 6:
        Serial.println("Failed to Receive Reset Complete");
        delay(1000);
        bno08x_hw_reset(&bno);
        break;
      default:
        break;
    }
  }

  uint8_t sensitivity = 0;
  uint32_t interval = rate;
  uint32_t configWord = 0;
  
  Serial.println("BNO Begin Feature Set");

  // Enable Sensor
  while(bno08x_feature_set(&bno, report, 0, sensitivity, interval, 0, configWord))
    Serial.println("BNO Failed to set feature report");
  
  Serial.println("BNO Begin MECal Config");

  // Enable/disable calibration routines
  if (bno08x_mecal_config(&bno, 1, 1, 1, 1, 0))
    Serial.println("BNO Cal failed");

  Serial.println("BNO Finished MECal Config");

  timer_start(&loop_timer); // Start loop timer

  #ifdef USE_POLL
  timer_start(&poll_timer); // Start poll timer
  #endif
  
  // Generate interrupt handler
  bno_int = interrupt_init(BNO_PIN_INT, GPIO_LOW, bno_ISR); // Interrupt is active low
    if (bno_int == NULL)
      Serial.println("BNO Failed to init interrupt");
  
  // Enable interrupt
  interrupt_set(bno_int);
  
  Serial.println("Begin loop()");

}

void loop() {
  
  // Read messages if received interrupt or poll timer expires
  #ifdef USE_INT
  if (bno_msg == 1){
  #endif

  #ifdef USE_POLL
  if (timer_check_exp(&poll_timer)){
  #endif

    bno_msg = 0;

    // Read, parse, and store messages on the handler
    bno08x_get_messages(&bno);

    // Process report data and add to struct
    get_data(&bno, &bno_data);

    #ifdef USE_POLL
    timer_reset(&poll_timer);
    #endif

    #ifdef USE_INT
    // Re-enable after reading the message
    interrupt_enable(bno_int);
    #endif
  }
  
  if (timer_check_exp(&loop_timer) != 0){
    #ifdef USE_INT
    interrupt_disable(bno_int);
    #endif
    
    timer_reset(&loop_timer);

    Serial.print("Accuracy: ");

    switch (bno_data.status){
      case 0:
        Serial.print("Unreliable");
        break;
      case 1:
        Serial.print("Low");
        break;
      case 2:
        Serial.print("Medium");
        break;
      case 3:
        Serial.print("High");
        break;
      default:
        break;
    }

    Serial.print(", ");        
    Serial.print("x: "); Serial.print(bno_data.x + 0.0005f,4); Serial.print(", ");
    Serial.print("y: "); Serial.print (bno_data.y + 0.0005f,4); Serial.print(", ");
    Serial.print("z: "); Serial.print (bno_data.z + 0.0005f,4); Serial.println();
    
    #ifdef USE_INT
    interrupt_enable(bno_int);
    #endif
 }

  if (bno_data.status == 3)  gpio_toggle(LED);
  else gpio_write(LED, LOW);


}
