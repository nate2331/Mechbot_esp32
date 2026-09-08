# IMU test with repeating robot movement

Standalone **NULLLAB Maker ESP32 Pro / ESP32 Dev Module** sketch for testing the
5 V STRDC BNO085 UART-RVC stream through the HiLetgo level converter while all four
motors operate. It also works with the existing SN74LVC245AN signal circuit.

## Run

1. Keep motor power off during upload. For the first run, support the robot with
   all wheels clear. Disconnect the Pi serial sender while using Serial Monitor.
2. Open `Maker_IMU_RVC_Movement_Test.ino` from this folder. Keep its two `.h`
   files alongside it. Select **ESP32 Dev Module**, Espressif ESP32 core **3.x**
   (compiled with **3.3.11**), and the Maker's port (previously **COM8**).
   Keep **Erase All Flash Before Sketch Upload disabled**.
3. Upload, then open Serial Monitor at **115200 baud**, **Newline** line ending.
   Boot is stationary. Check `MOTOR_CHECK ok=1`, `UART_CHECK ok=1`, and IMU
   `state=READY` with changing readings and clean counters.
4. Enable motor power when positioned for the test. Send **`GO`**. A three-second
   countdown precedes movement. **`X` stops immediately**, including during the
   countdown; it does not need a newline to stop. Send a fresh `GO` line to restart.

The repeating sequence is:

| Phase | Duration |
| --- | --- |
| Forward | 1 second |
| Coast | 0.3 seconds |
| Backward | 1 second |
| Coast | 0.3 seconds |
| Strafe left | 1 second |
| Coast | 0.3 seconds |
| Strafe right | 1 second |
| Coast, then repeat forward | 0.3 seconds |

Each motion window includes about **177 ms of ramp-up** to duty 150. PWM is
**248 Hz, logical duty 150/256 (about 58.6%)**, identical on all wheels. Hardware
uses 9-bit PWM with duty 300/512 to produce that same duty fraction. It is
open-loop: unequal wheel speeds or traction can make the robot drift instead
of returning to its starting position. First confirm wheel direction while
raised, then use a clear area for a supervised floor run. The physical motor
power switch remains the way to stop without the serial connection.

`s` prints a snapshot and `?` prints help. `GO` is a complete line, case-insensitive.
Unknown complete commands stop movement. The sequence continues until `X`, a
fault, reset or power-off; disconnecting Serial Monitor alone is not a stop command.

## Wiring

Keep the working **UART-RVC** hookup; the old integrated firmware's SPI wiring
does not apply to this test.

| HiLetgo pin | Connection |
| --- | --- |
| HV | Maker 5 V |
| LV | Maker 3.3 V |
| GND | Common Maker/IMU ground |
| HV1 | IMU SDA/TX |
| LV1 | Maker GPIO21 |

IMU VDC stays at 5 V and P0 stays strapped to its own VDC. Leave other mode straps
as in the working continuous test. Unused converter channels stay disconnected.
Leave the proposed reset transistor's GPIO25 and IMU RST connections disconnected
for this test; the sketch does not drive reset, mode or IMU power pins.

| Wheel | Motor port | Driver inputs | Forward active input |
| --- | --- | --- | --- |
| FL | M2 | 17 / 12 | 17 |
| FR | M3 | 14 / 15 | 14 |
| RL | M1 | 4 / 2 | 4 |
| RR | M0 | 27 / 13 | 13 |

M2/M3 switches must be in Motor mode. The source uses the established RR motor
polarity inversion and logical wheel signs from `Maker_Mechbot`. Encoder pins
are unused by this sketch.

## Read the results

Each second includes the familiar `RUN`, `COUNTS`, `UART_EVENTS` and `VALUE`
records, plus `MOTION` with direction, cycle and actual logical PWM outputs.
`EVENT MOVE` and `EVENT COAST` mark the transitions so errors can be related to
motor startup and direction changes. Normal operation is approximately 100 RVC
frames per second, fresh readings and zero new checksum/UART/index errors.
Index discontinuities count unexpected sequence numbers, not exact lost frames.

The IMU qualifies after five sequential valid frames no more than 100 ms apart.
More than 500 ms without a valid frame stops and latches the motion test. IMU
listening continues so recovery is visible, but recovery **does not restart
movement**; another `GO` is required. Occasional checksum/index/UART errors are
logged without stopping unless the valid stream becomes stale. This lets the
test expose intermittent corruption under motor load.

A separate output task runs about every 5 ms. It enforces the one-second motor
window, gradual ramp, reversal pause and 300 ms main-loop command lease even if
the main loop is blocked. A lease timeout latches a stop. PWM/task initialization
failure prevents starting. Zero output coasts; this is not active braking,
current limiting or stall detection. No heading correction or automatic IMU
reset is applied.

## Implementation and validation

- `RvcParser.h` is copied unchanged from `Maker_IMU_RVC_Continuous_Test`.
- `MotorSafety.h` is copied unchanged from `Maker_Mechbot`.
- The original passive test and robot firmware are not changed by this sketch.
- 248 Hz/duty 150 follows the later September 6 PWM experiments. The integrated
  source's older 20 kHz/177 settings are not used. This build uses 80 MHz APB,
  9-bit resolution and doubled hardware duties, matching the later bench sweeps.
  Classic ESP32 has no LEDC XTAL clock option; 248 Hz at 8-bit resolution also
  exceeds its APB divider range. The ramp and logged duty retain logical 8-bit
  units; multiply them by two for hardware duty-register values.
- Target compilation and simulated tests are recorded in `BUILD_STATUS.md`.
  Those checks do not establish physical direction, wiring, motor-noise
  tolerance or measured stop timing. The first hardware run is still required.

