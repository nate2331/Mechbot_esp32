# PWM driver and motor source notes

Archive note (September 8): these September 5 source notes precede the completed frequency sweeps. The [duty-150 fine sweep](fine-duty150-2026-09-06/RESULTS.md) did not show a large speed collapse across the nominal sleep boundary. See the [archive overview](README.md) for the measured conclusions and remaining limits.

Research date: 2026-09-05. Read-only research; no hardware commands executed.

## What the actual board documentation establishes

[NULLLAB Maker ESP32 Pro README](https://github.com/nulllaborg/maker-esp32-pro) names the driver SS6625E and lists M0=(27,13), M1=(4,2), M2=(17,12), M3=(14,15). However, its [linked schematic](https://github.com/nulllaborg/maker-esp32-pro/blob/main/maker-esp32-pro.pdf), motor sheet/page 4, labels every driver **RZ7889**, revision V1.6. Readme versus schematic is an unresolved component/revision mismatch; chip markings on both boards must establish which timing specifications apply.

Schematic inspection finds:

- All four bridges share MOT_VIN, directly connected to VIN, and AGND. There is no separate front/rear motor supply drawn.
- The motor rail has one 470 uF bulk capacitor (C32) and four 0.1 uF capacitors (C17/C10/C22/C23).
- All eight driver inputs have 1 kOhm series resistors. M2/M3 additionally pass through SW1; M0/M1 do not. No input shunt capacitors or deliberate RC filters are drawn.

This topology does not establish that the real board has identical trace impedance, ground bounce, switch contact condition, or component population. It provides specific locations to measure.

## SS6625E: published behavior and limits

[Leadpower's own product page](https://www.leadpoweric.com/productshow51.html) and [manufacturer-hosted brief](https://www.leadpoweric.com/uploads/admin/file/20250527/8cddb6e72c2a716168c1a2274b0f9121.pdf) establish the part. The latter has only two pages. A [complete manufacturer-authored V1.0 2025 datasheet, mirrored by Datasheet4U](https://datasheet4u.com/pdf/1596900/SS6625E.pdf) has ten pages; mirror provenance and board identity remain caveats.

The complete document specifies:

| Item | Published behavior/value | Page |
|---|---|---|
| FI=H, BI=L | FO=H, BO=L; forward | 5 |
| FI=L, BI=H | FO=L, BO=H; reverse | 5 |
| FI=BI=H | Both outputs low; brake | 5 |
| FI=BI=L | Outputs open/coast; sleep after delay | 5 |
| Sleep entry | 1.7 ms typical, inputs both low | 4, 8 |
| Wake/start time | 60 us typical; first power-up/exit sleep only | 4, 8 |
| Logic input frequency | 0-125 kHz recommended range | 3 |
| PWM supply-current test | 50 kHz on BI, FI held high, no load | 4 |
| Overcurrent | 16 A typical; 3.5 us qualification; 4.7 ms retry | 4, 7 |

Sleep/wake entries have **no minimum or maximum**; table conditions are 25 C. No ordinary PWM propagation delay, minimum pulse width, input filter, or internal dead time is specified. The PWM test uses drive/brake, not drive/coast. Thermal thresholds disagree between table and prose, another reason to avoid overprecise protection modeling.

## Derived sleep boundary, not an optimal-frequency proof

Assuming ordinary 8-bit LEDC duty is `d/256`, one input PWM and the other held low:

`t_off = (1 - d/256)/f`; remaining above the typical sleep boundary requires `f > (1 - d/256)/0.0017`.

| Duty count | Duty fraction | Off time at 248 Hz | Frequency at typical sleep boundary |
|---|---:|---:|---:|
| 175 | 0.68359375 | 1.275832 ms | 186.121 Hz |
| 150 | 0.58593750 | 1.669607 ms | 243.566 Hz |
| 149 | 0.58203125 | 1.685358 ms | 245.864 Hz |
| 148 | 0.57812500 | 1.701109 ms | 248.162 Hz |

At 248 Hz, the boundary is 148.0704 counts. Thus 149 and above nominally avoid sleep; 148 and below can enter sleep under typical specifications. Duty150 has only 30.4 us (~1.8%) margin to the typical sleep delay. This is **not a guaranteed operating margin** across chips, temperature or supply.

At duty175, 20 kHz off-time is 15.820 us and 5 kHz off-time is 63.281 us. Both are far below 1.7 ms. The published sleep mechanism therefore does not explain the established speed improvement from 20 kHz to 248 Hz at duty175, nor an earlier 20 kHz > 5 kHz result. Do not subtract 60 us from every PWM pulse: the datasheet restricts that delay to wake/start.

If genuine sleep occurs every cycle, 60 us wake loss at 248 Hz corresponds to about 1.49 percentage points of duty. This simple estimate assumes wake delay removes that duration from an otherwise valid high pulse; scope evidence is required. The mathematical proximity of 248 Hz to the duty150 sleep boundary is interesting but does not turn an accidental software constant into a scientifically optimized number.

## Motor parameters and missing measurements

The [OSOYOO simplified motor datasheet](https://osoyoo.com/picture/flexirover/2024007400/2024005900.pdf) heading is model2024005900, while its performance table says2024005800. Page1 gives 56:1 reduction, 6-15 V operating range, 12 V nominal, no-load178 rpm +/-10% and0.13 A, maximum-efficiency150 rpm/0.6 A/0.2 Nm/3.2 W, starting1 Nm/2.95 A. Page3 specifies11 PPR with two A/B channels and3.3-5 V encoder supply. The four-page simplified sheet does not supply winding inductance, electrical time constant, rotor/load inertia, or measured terminal resistance.

An approximate effective starting resistance is `12 V / 2.95 A = 4.07 Ohm`, conditional on the table applying to the installed motor and rated-voltage starting conditions. Brush drop, winding temperature, rotor position and table tolerances prevent treating this as a precision winding-resistance measurement. Without L, R, current waveform and loaded torque, neither an exact electrical-time-constant prediction nor an optimum PWM frequency can be obtained from this document.

## RZ7889 fallback caveat

An [older manufacturer-authored RZ7889 datasheet, mirrored by distributor Micros](https://www.micros.com.pl/mediaserver/UIRZ7889_0001.pdf) agrees on the four input/output states, but does not specify the SS6625E sleep timings. Do not apply the 1.7 ms boundary if actual markings show RZ7889 without obtaining the matching revision's timing data or measuring it.

## Original local evidence

The original research workspace retained `maker-esp32-pro.pdf`, `ss6625e-full.pdf`, `osoyoo-2024005900.pdf`, and `rz7889.pdf`, together with rendered timing tables, sleep traces and the motor schematic. Critical pages were rendered and visually inspected during that research. These manufacturer PDFs and rendered copies are not redistributed in this archive; public citations above preserve their provenance. At the research date, the direct download for the complete SS6625E PDF was the `/pdf/1596900/SS6625E.pdf` URL above; the older `/download_new.php?id=1596900` route returned HTML in local HTTP downloads.
