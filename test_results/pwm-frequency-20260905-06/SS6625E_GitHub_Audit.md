# SS6625E PWM: GitHub evidence audit

Archive note (September 8): this audit assesses external projects, not the subsequent local trial results. The proposed local frequency comparisons were completed on September 6; see the [archive overview](README.md) and linked raw datasets. Its conclusion that the reviewed external projects do not establish an optimum remains the scope of this source audit.

**Prepared for Nate · September 6, 2026**

## Finding

The reviewed public projects do **not** establish a consensus optimum for SS6625E PWM, and none provides an independent controlled experiment establishing 248 Hz as optimal. They do provide useful implementation precedents: 1 kHz, 5 kHz, and 20 kHz appear in code or lessons. Their evidential strength differs substantially.

The strongest new problem-specific lead is an SS6625E teaching lesson recommending 5 kHz and warning that 20 kHz may leave a motor stationary. This is a troubleshooting claim without published measurements, not a demonstrated universal chip limit. A separate robot implementation explicitly naming SS6625E uses 1 kHz. The article Nate supplied also implies 1 kHz through ESP32 analogWrite defaults.

## What the projects actually establish

| Source | Requested PWM / duty | Hardware association | Assessment |
|---|---|---|---|
| Inouye165 robot tank | 1,000 Hz, 8 bits; variable duty | Author explicitly names SS6625E on Maker ESP32 Pro | Separate implementation with reported empirical controller tuning; no frequency comparison |
| WorkWebsitesMixed teaching boat | 5,000 Hz, 10 bits; lesson caps output at 60% | Explicit SS6625E | Explicit 20 kHz stall warning; educational skeleton and test instructions, no completed measurement record |
| NULLLAB board example | 5,000 Hz, 8 bits; supplied loop commands only ±255 | README explicitly names SS6625E | Vendor configuration precedent; does not establish performance at ordinary partial duty |
| Emakefun MD02 demo | Uno analogWrite only 0 and 255 | Vendor explicitly identifies two SS6625E chips | Static LOW/HIGH example, not a PWM-frequency experiment |
| Marco5507 RC boat | 20,000 Hz, 8 bits; minimum nonzero target 70 | Maker ESP32 Pro family; fitted chip unverified | Code precedent with no measured PWM-validation results |
| Xaionaro ESPHome baseline | 20,000 Hz, 10 bits; duty 307 | Maker ESP32 Pro family; fitted chip unverified | Status explicitly says no motor command was issued |
| Xieweichin BadeCar | 20,000 Hz, 10 bits | Board family only; supplied image shows RZ7889 | Code intent, not verified SS6625E operation |
| Emakefun encoder library | 248 Hz after integer conversion, 10 bits | Vendor-linked library, not an independent hardware study | Explains the software origin of 248; does not establish its physical optimality |

### 1. Inouye165: useful 1 kHz precedent

The actual setup requests 1,000 Hz and eight-bit resolution on all eight motor-control inputs. During motion, one input receives PWM and the other stays LOW; stopping sets both LOW. Source comments name SS6625E, but there is no chip photograph. [Motor implementation](https://github.com/Inouye165/esp-maker-usba-4motor/blob/ba76ff04e18f898667c4f8e7f14f8eea9b2d762b/src/MotorDriver.cpp#L26), [chip identification](https://github.com/Inouye165/esp-maker-usba-4motor/blob/ba76ff04e18f898667c4f8e7f14f8eea9b2d762b/src/RoverConfig.cpp#L19).

The setting already existed in the initial June 23 commit. Later material describes empirical motion tuning, but PR #4's passing tests are controller simulations. There is no published frequency sweep, waveform, current trace, or start-success dataset. [Initial implementation](https://github.com/Inouye165/esp-maker-usba-4motor/blob/a72f740a36114076b0155487388e8431766127b9/src/main.cpp#L1250), [PR #4](https://github.com/Inouye165/esp-maker-usba-4motor/pull/4).

### 2. Teaching boat: the closest corroborating symptom

The lesson specifies 5 kHz and ten-bit resolution, and warns that much faster PWM, specifically 20 kHz, can leave the motor stationary. Its central motor-output method is an exercise containing TODOs. Its checkpoints describe what students should observe; they are not recorded outcomes. [Drive the Paddle Motor lesson](https://github.com/WorkWebsitesMixed/andys-world/blob/6712bbbf9dd79623060d6b43d2edda67918c816e/src/content/block-sessions/1a/e2.mdx).

The lesson entered in one August 25 content import described as adapted from course notes. The public history contains no before/after frequency experiment, and the shared test plan lists future pass criteria. This warrants investigating the symptom, without adopting the lesson's explanation as proven. [Import commit](https://github.com/WorkWebsitesMixed/andys-world/commit/f3d83c1d76d47377974f6f0d8047de75ef9532a6), [test plan](https://github.com/WorkWebsitesMixed/andys-world/blob/6712bbbf9dd79623060d6b43d2edda67918c816e/src/content/block-sessions/1a/test-plan.mdx).

### 3. Vendor examples: maximum output is a weak PWM test

NULLLAB's SS6625E board README links a motorTest ZIP configuring 5 kHz/eight bits. Its supplied loop only commands maximum positive and negative output. That does not validate the partial-duty operating region involved in our problem. [Board README](https://github.com/nulllaborg/maker-esp32-pro/blob/f2fa83889ee5feb0ebf1eca8e23c018e87c9fd44/README.md), [linked motor sample](https://github.com/nulllaborg/maker-esp32/releases/download/v1.0.0/motorTest.zip).

MD02 is explicitly an SS6625E module, but its Uno example only calls analogWrite with 0 and 255. Arduino's AVR implementation special-cases those values to digital LOW/HIGH. It would be incorrect to count this as measured success at Uno's usual PWM frequencies. These vendor materials also belong to the same supplier ecosystem, so they are not independent experimental replications. [MD02 documentation and sample](https://github.com/emakefun/emakefun-docsify/blob/0a09ef04d3c5b1bcf2a5863cdd75e7ee13cab13b/docs/zh-cn/ph2.0_sensors/actuators/md02/md02_zh-cn.md), [Arduino implementation](https://github.com/arduino/ArduinoCore-avr/blob/11b9130371e8447920edb65a75706a6c951e51fc/cores/arduino/wiring_analog.c#L104).

### 4. Why the 20 kHz repositories do not settle the disagreement

Marco5507's boat configures 20 kHz/eight bits, variable duty, and a 70-count minimum target. Ramping still passes through lower commands. The inspected material does not identify the fitted chip or publish frequency measurements. [Boat sketch](https://github.com/marco5507/maker-esp32-pro-rc-boat/blob/fafef5d0bed040ba295ed494aa76b6773cf7e9ca/rc_boat_ps3/rc_boat_ps3.ino).

Xaionaro's 20 kHz baseline was compiled, flashed, and booted, but its status explicitly states that no motor command was issued and powered behavior remained unverified. A successful build and boot cannot demonstrate motor performance. [Implementation status](https://github.com/xaionaro/my-devices/blob/5c8e5b601dcef035ae889a09dddc15a829f8cbe6/IMPLEMENTATION_STATUS.md), [PWM constants](https://github.com/xaionaro/my-devices/blob/5c8e5b601dcef035ae889a09dddc15a829f8cbe6/components/maker_esp32_pro/maker_esp32_pro.h#L32).

BadeCar sets 20 kHz/ten bits, but its stock product image shows RZ7889, not SS6625E. Its bundled encoder header also contains apparent source defects; it was not compiled during this audit. This cannot be counted as an SS6625E hardware-success vote. [Constants](https://github.com/xieweichin/BadeCar/blob/b4320d0f57732003ca903043dcc8d96c96f4c4cc/lib/motor.h#L53), [board image](https://github.com/xieweichin/BadeCar/blob/b4320d0f57732003ca903043dcc8d96c96f4c4cc/assets/badecar.png), [encoder header](https://github.com/xieweichin/BadeCar/blob/b4320d0f57732003ca903043dcc8d96c96f4c4cc/lib/encoder_motor.h).

### 5. The supplied Korean article

SwMaker_Jun's May 17, 2026 article names SS6625E and uses analogWrite with command 200, leaving frequency unspecified. In the checked Espressif 3.0.0 and 3.3.11 implementations, defaults are 1,000 Hz/eight bits. Thus its standalone motor example implies 1 kHz, not 248 Hz; no measured frequency comparison is provided. [Article](https://swmakerjun.tistory.com/140), [Espressif 3.0.0](https://github.com/espressif/arduino-esp32/blob/3.0.0/cores/esp32/esp32-hal-ledc.c#L359), [Espressif 3.3.11](https://github.com/espressif/arduino-esp32/blob/3.3.11/cores/esp32/esp32-hal-ledc.c#L825).

### 6. What is definite about 248

The linked encoder library declares kPwmFrequency as uint8_t but initializes it to 75000. Unsigned eight-bit conversion gives 75000 modulo 256 = 248. The initialization code passes that value into LEDC setup. The currently retrieved main-branch header matches the pinned source. This establishes the software origin of the requested number, not a motor-specific optimum. [Frequency declaration](https://github.com/emakefun-arduino-library/em_esp32_encoder_motor/blob/29ef1c31f3c0b0bc51cc279b74e97a64a4e4ccf5/src/motor.h#L48), [LEDC initialization](https://github.com/emakefun-arduino-library/em_esp32_encoder_motor/blob/29ef1c31f3c0b0bc51cc279b74e97a64a4e4ccf5/src/motor.cpp#L27).

## Implication for our investigation

There is limited precedent for 1–5 kHz, including one warning about 20 kHz, but no justified universal frequency recommendation. Requested frequency in code is not a measured waveform; different motor loads, board revisions, duty ratios, and current-decay modes prevent treating these projects as a pooled experiment.

Keep our observed 248 Hz result as the working reference. The most informative new comparison from this search is 1 kHz. A controlled comparison should preserve motor, output port, supply, load, drive/coast behavior, and actual duty ratio, and measure input PWM plus motor current or differential output voltage. To establish that 248 itself matters rather than a broad low-frequency region, neighboring frequencies also need comparison. These are proposed measurements, not completed tests.

## Search coverage and stopping point

Searches used GitHub code search for SS6625E, SS6625, 6625E, chip/PWM combinations, chip/248 combinations, and Maker ESP32 Pro naming, followed by actual source, documentation, and selected history/PR audits. The initial exact-chip search returned six files across four repositories; board-name searches added three relevant project repositories. Vendor-linked downloads and the encoder library were followed separately.

Exact SS6625E issue and pull-request queries returned zero results. The broader 6625E issue query returned 26 unrelated numerical/scientific-notation matches. Broader code searches also produced many hash and numeric false positives, which were excluded. External web search and the user's article supplemented GitHub discovery.

This is a bounded indexed search, not an exhaustive census of GitHub. No reviewed source supplied the independent controlled PWM-frequency evidence needed to establish an optimum. Research stopped after the plausible projects and contradictory 20 kHz examples were audited; additional repetitions of configuration constants would not resolve that measurement gap.
