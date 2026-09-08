# Maker PWM Frequency Test

A standalone test for Nate's **Maker ESP32 Pro / SS6625E board** and existing four encoder motors. It powers **one selected wheel at a time**, using the wiring and calibration from the previous bare 248 Hz sketch.

## Open and run

1. Open **Maker_PWM_Frequency_Test.ino** in Arduino IDE. Keep it inside the **Maker_PWM_Frequency_Test** folder.
2. Select **ESP32 Dev Module**, with **esp32 by Espressif Systems 3.x**. Validation uses **3.3.11**. No additional libraries are needed.
3. Keep the wheels raised and use the same regulated **9 V supply setup** as the earlier tests. The motor-power switch must be ON; M2/M3 switches must select motor output. The sketch does not measure supply voltage.
4. Upload when ready. Open Serial Monitor at **115200 baud**, line ending **Newline**. Boot leaves every motor output LOW.
5. Start with one trial: **run 248 f**. Wait for **# IDLE** before issuing another command. Then try **run 248 r**.

**x** or **!** stops immediately when received, even before a newline. Each successful trial ends automatically after eight seconds. If the encoder produces no valid transitions for 1.5 seconds, the trial and remaining sequence stop with **no_encoder_edges**. Check the motor and encoder before restarting: that condition cannot distinguish a stationary motor from an encoder fault.

This deliverable is source code and validation results. Creating or compiling it does not upload it or operate the robot.

## Commands

Send one command per line. Configuration changes take effect only while IDLE.

| Command | Meaning |
|---|---|
| run 248 f | One forward trial at 248 Hz |
| run 1000 r | One reverse trial at 1,000 Hz |
| sweep | 248, 1,000, 5,000 and 20,000 Hz; both directions in every block |
| fine | 200, 240, 248, 250, 256, 300, 500 and 1,000 Hz; both directions |
| blocks 3 | Three repetitions of every frequency/direction condition; allowed 1–10 |
| seed 2482026 | Set reproducible nonzero random seed |
| wheel fl | Select FL; alternatives fr, rl, rr |
| duty 175 | Duty fraction 175/256; allowed 1–254 |
| profile history | Eight-bit AUTO clock; reproduces the previous requested settings |
| profile apb | Nine-bit APB clock; holds clock source and resolution fixed across the sweep |
| profile ref | Nine-bit REF_TICK clock; accepts 200–1,900 Hz |
| ? | Print commands and current settings |
| stop | Stop and cancel the remaining plan |
| x or ! | Stop without waiting for newline |

**Default:** FL, history profile, duty 175/256, three blocks, seed 2482026.

Any completed command during an active sequence cancels it; the command must then be sent again while IDLE. Overlong input also stops the sequence. After an emergency character sent without a line ending, send a newline before the next command.

The ordinary **sweep** has 24 trials with the default three blocks and takes approximately 4½ minutes if all trials complete. The **fine** plan has 48 trials and takes approximately nine minutes. Both shuffle the conditions separately inside each complete block. The seed and complete planned order are printed before motion.

## Two distinct comparisons

First reproduce the observed behavior:

    profile history
    duty 175
    blocks 3
    sweep

Send the settings while idle, then the final sweep command. This uses AUTO clock selection. On the audited classic ESP32/core, the clock source can change between the low and high frequencies. The output therefore labels the setting **clock_requested=AUTO**, without pretending that AUTO is a physical clock source.

Then repeat while holding the clock source fixed:

    profile apb
    sweep

This profile uses nine-bit PWM. A command of **duty 175** becomes **350/512**, preserving exactly the historical **175/256 = 68.359375%** duty fraction. Duty 150 similarly becomes 300/512. It does not use 175/255 or silently change the command fraction.

Keep the two profiles separate in analysis. To investigate the resolution/clock transition at 248 Hz, compare individual trials using history, ref, and apb profiles before attributing a difference solely to frequency. The **ref** profile supports the **fine** plan, but rejects the ordinary **sweep** because 5 kHz and 20 kHz are not attainable at nine-bit REF_TICK.

Use **fine** to investigate whether 248 differs from nearby frequencies. The default duty is held constant. A separate duty-145–153 study around 248/250 Hz can investigate the proposed sleep boundary; do not mix those results into the constant-duty frequency comparison.

## Wiring preserved from the earlier tests

| Physical wheel | Board ports | Driver inputs | Encoder A/B | Motor sign | Encoder sign | Historical quadrature counts / wheel turn |
|---|---|---|---|---|---|---|
| FL | M2 / E2 | 17, 12 | 35, 36 | +1 | +1 | 2468.8 |
| FR | M3 / E3 | 14, 15 | 34, 39 | +1 | −1 | 2467.9 |
| RL | M1 / E1 | 4, 2 | 5, 23 | +1 | +1 | 2473.5 |
| RR | M0 / E0 | 27, 13 | 18, 19 | −1 | −1 | 2469.8 |

These are the robot's previous assignments, including the FR input ordering and RR polarity. They are not a claim that arbitrary motors arrive with these directions or calibrations. If wiring, motor identity, gearing or encoder interpretation changes, update the **wheels** table. GPIO 34–39 use INPUT because the classic ESP32 provides no internal pull-ups on those pins.

Raw signed counts and valid/invalid transitions remain available regardless of the RPM calibration.

## Measurement and output

No PID, kickstart, minimum-duty adjustment, acceleration ramp, gamepad, Wi-Fi, servo or IMU code runs. A trial steps directly from rest to the selected duty. One driver input receives PWM and the other remains LOW: drive/coast operation. All other motor inputs remain LOW.

Every trial requires at least three seconds off and 500 ms of encoder quiet. Noise or continuing motion delays the next trial; failure to become quiet within 15 seconds cancels the sequence. The actual off interval is logged. At each stop Espressif's driver disables all eight PWM channel outputs and forces idle LOW; it does not brake. Channel assignments stay fixed to avoid restoring stale duty during reattachment.

Encoder snapshots are buffered approximately every 100 ms. All CSV reporting happens **after outputs are stopped**, so serial output does not consume time during powered sampling. The log contains:

- **#** lines: configuration, core/SDK versions, order, status and pin assignments.
- **summary** header/data rows: condition, actual command-relative duration, count totals, invalid transitions, first observed valid-edge latency, whole-run RPM, first-five-second RPM and tail RPM.
- **sample** header/data rows: cumulative signed ticks, valid transitions and invalid transitions versus actual microsecond timestamps.

Repeated CSV headers are intentional: every trial's block can be copied independently. Save the **entire Serial Monitor log** as plain text for analysis. Do not discard interrupted or failed trials.

The five-second metric ends at the first captured sample at or after five seconds; its actual endpoint is reported. The tail metric uses that sample through the final pre-stop snapshot, typically about seconds 5–8. **Tail RPM is not automatically settled RPM.** Inspect the samples to determine whether speed was still changing.

If a trial ends early, missing five-second and tail metrics are **blank**, not zero. Reverse RPM/counts retain their sign. Use magnitudes when comparing forward and reverse speed.

**first_edge_observed_us** is software-observed latency at roughly loop resolution, not an oscilloscope edge measurement or proof of sustained startup. The no-edge watchdog also applies if transitions cease after startup.

Frequency readback comes directly from the owned timer through ledc_get_freq while outputs are still stopped. It is integer peripheral readback, not an independently measured waveform. Configuration, output-write and readback failures cancel the plan; PWM API failures latch a fault requiring reset.

PWM duty updates take effect at a cycle boundary. The selected channel's first cycle can reflect its previous duty before the new value latches; this uncertainty is at most approximately one PWM period during normal timer operation. Timestamps are therefore command-relative, and exact first-pulse timing still requires an oscilloscope. Other channels remain disabled.

## What this can and cannot establish

The sketch can distinguish a slow start from a persistent speed difference and provide repeated, consistently labeled measurements. It cannot measure motor current, driver output voltage, supply sag or temperature without additional instruments. Zero invalid transitions does not exclude missed complete quadrature cycles.

Keep motor, port, load, wiring, supply setup and initial thermal conditions consistent. Record any changes alongside the log. Four-wheel supply interactions require a separate experiment; this sketch intentionally isolates one output.

The wheel assignments and calibration come from the earlier bare 248 Hz test. PWM wrapper behavior was checked against [Espressif's Arduino LEDC implementation, 3.3.11](https://github.com/espressif/arduino-esp32/blob/3.3.11/cores/esp32/esp32-hal-ledc.c); this sketch uses the bundled ESP-IDF driver directly.
