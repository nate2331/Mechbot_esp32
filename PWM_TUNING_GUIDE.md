# Blackbot Two PWM tuning guide

This procedure is designed for the robot's current sensor configuration: rear
left and rear right encoders only. It tunes the four open-loop PWM ceilings; it
does not create closed-loop wheel-speed control.

## What the page can infer

- RL encoder response can be compared with earlier RL response.
- RR encoder response can be compared with earlier RR response.
- A floor observation of nose rotation and off-axis path drift can identify a
  useful four-wheel trim through the mecanum mixing geometry.
- RL raw ticks cannot yet be treated as the same distance unit as RR raw ticks.
  The page therefore never divides them into a motor-match ratio.

The PWM value is a wheel's full-command ceiling. A controlled test multiplies
that ceiling by the selected output. For example, a rear PWM ceiling of 200 at
45% output produces an approximate duty of 90. The page previews this duty for
all four wheels before the test.

## 1. Wheels-up response check

1. Secure the chassis so all four wheels are clear of the floor.
2. Open **Rear encoder response check** and confirm the wheels-up condition.
3. Start at 15% output for 0.75 seconds. Increase output one level at a time
   until both rear channels report nonzero, repeatable response without jerking.
4. Repeat an identical direction/output/duration before judging a change. The
   page suppresses percentage comparisons if output or duration changed.
5. Use this step to find a reliable test threshold and diagnose dead, reversed,
   or intermittent rear encoder response. Do not try to match RL rate to RR rate.

## 2. Guided floor balance

1. Use one flat surface, a consistent battery state, and a marked start line.
2. Load the proven `230 / 230 / 200 / 200` baseline.
3. Select **Forward**, 45% output, and 1.0 second for the first test.
4. After the automatic stop, record two separate observations:
   - whether the robot's nose rotated left/counterclockwise or right/clockwise;
   - whether the path drifted off-axis.
5. Start with a 3-count suggested trim. Use 5 only for a pronounced error and
   1–2 for final refinement.
6. Review the proposed PWM values. Applying them is never automatic.
7. Repeat the exact same direction, output, and duration. Compare each rear
   encoder with its own prior rate and score the physical path again.
8. Keep the better result. Use **Restore #N PWM** after a worse trial, **Use #N
   PWM** in history to restore any tested set, or **Undo last PWM change** before
   retesting. Require at least two consistent runs before treating a setting as
   a winner.
9. Tune in this order: forward, reverse, strafe left, then strafe right. A trim
   that helps only one direction is not yet a general winner.

Heading correction stays disabled during motor tests so it cannot hide an
open-loop motor imbalance. **STOP NOW** aborts motion but retains the active
session and live PWM values, allowing inspection or a safe resume.

## 3. Keep, restore, or save

- **Finish · keep live only** ends exclusive tuning control without writing NVS.
- **Restore originals & finish** returns every setting captured at session start.
- **Save PWM & finish** restores the original heading-enabled state first, then
  writes the winning live configuration to ESP32 nonvolatile storage.

Do not save after a single good-looking run. Save only after repeatable results
in both longitudinal directions and both strafe directions.

## Remaining limitation

True automatic wheel-speed matching requires normalized encoders on all four
wheels (or a characterized counts-per-revolution scale for the existing rear
channels) and per-wheel closed-loop control in firmware. The current workflow
extracts the most defensible tuning signal available without pretending the two
rear raw tick scales are interchangeable.
