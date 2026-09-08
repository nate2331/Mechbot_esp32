#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include "../Maker_Mechbot/MotorSafety.h"
#include "../Maker_Mechbot/NavigationMath.h"

using MotorSafety::Controller;
void stopped(const Controller &c) {
  for (const auto &w : c.wheels) assert(w.applied == 0);
}
void testTimeout() {
  Controller c;
  const float forward[] = {177, 177, 177, 177};
  assert(c.publish(forward, 0));
  for (uint32_t t = 5; t < 300; t += 5) {
    assert(!c.tick(t));
    for (const auto &w : c.wheels) assert(w.applied >= 0 && w.applied <= 177);
  }
  assert(c.tick(300));  // No I2C or main-loop progress needed for timeout.
  stopped(c);
  assert(!c.live);
  assert(!c.tick(500));
  stopped(c);
}
void testRampAndStop() {
  Controller c;
  const float target[] = {177, -177, 89, 0};
  assert(c.publish(target, 0));
  c.tick(5);
  assert(std::fabs(c.wheels[0].applied - 4.25F) < 0.001F);
  c.tick(200);  // Late tick must not jump to target.
  assert(c.wheels[0].applied <= 12.75F);
  for (uint32_t t = 205; t <= 450; t += 5) {
    if (t % 50 == 0) assert(c.publish(target, t));
    c.tick(t);
  }
  assert(c.wheels[0].applied == 177);
  assert(c.wheels[1].applied == -177);
  assert(c.wheels[2].applied == 89);
  assert(c.wheels[3].applied == 0);
  c.stop(451);
  stopped(c);
  c.tick(600);
  stopped(c);
}
void testReversal() {
  Controller c;
  const float forward[] = {177, 177, 177, 177};
  const float reverse[] = {-177, -177, -177, -177};
  for (uint32_t t = 0; t <= 250; t += 5) {
    if (t % 50 == 0) c.publish(forward, t);
    c.tick(t);
  }
  assert(c.wheels[0].applied == 177);
  uint32_t firstZero = 0, firstNegative = 0;
  for (uint32_t t = 255; t <= 900; t += 5) {
    if (t == 255 || t % 50 == 0) c.publish(reverse, t);
    c.tick(t);
    const float duty = c.wheels[0].applied;
    if (duty == 0 && firstZero == 0) firstZero = t;
    if (duty < 0 && firstNegative == 0) firstNegative = t;
    if (firstZero && !firstNegative) assert(duty == 0);
  }
  assert(firstZero > 255);
  assert(firstNegative - firstZero >= MotorSafety::REVERSE_PAUSE_MS);
  assert(c.wheels[0].applied == -177);
  c.stop(901);
  c.publish(forward, 902);
  c.tick(906);
  stopped(c);  // X followed by reverse must not bypass the coast pause.
}
void testZeroAndBadInput() {
  Controller c;
  float values[] = {100, 100, 100, 100};
  c.publish(values, 0);
  c.tick(5);
  values[2] = 0;
  c.publish(values, 6);
  c.tick(10);
  assert(c.wheels[2].applied == 0 && c.wheels[0].applied > 0);
  for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(), 256.0F, -256.0F}) {
    values[1] = bad;
    assert(!c.publish(values, 11));
    stopped(c);
    assert(!c.live);
  }
}
void testClockWrap() {
  Controller c;
  const uint32_t start = UINT32_MAX - 100;
  const float target[] = {100, 100, 100, 100};
  c.lastTick = start;
  c.publish(target, start);
  for (uint32_t dt = 5; dt < 300; dt += 5) assert(!c.tick(start + dt));
  assert(c.tick(start + 300));
  stopped(c);
}
void testMixAndPolarity() {
  const float moves[][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  const int expected[][4] = {{1,1,1,1},{-1,-1,-1,-1},{-1,1,1,-1},
                            {1,-1,-1,1},{-1,1,-1,1},{1,-1,1,-1}};
  for (unsigned k = 0; k < 6; ++k) {
    float wheels[4];
    NavigationMath::mecanumMix(moves[k][0], moves[k][1], moves[k][2], wheels);
    for (unsigned i = 0; i < 4; ++i) assert(wheels[i] == expected[k][i]);
  }
  float wheels[4];
  NavigationMath::mecanumMix(1, 1, 1, wheels);
  for (float v : wheels) assert(std::fabs(v) <= 1);
  float yaw = 0;
  assert(NavigationMath::quaternionToYaw(0, 0, std::sqrt(0.5F), std::sqrt(0.5F), yaw));
  assert(std::fabs(yaw - NavigationMath::PI_F / 2) < 0.0001F);
  float forward, left;
  NavigationMath::fieldToRobot(1, 0, yaw, forward, left);
  assert(std::fabs(forward) < 0.0001F && std::fabs(left + 1) < 0.0001F);
}
int main() {
  testTimeout(); testRampAndStop(); testReversal(); testZeroAndBadInput();
  testClockWrap(); testMixAndPolarity();
  std::cout << "PASS: timeout, ramp, stop, reversal, zero, invalid inputs, clock wrap, mecanum and IMU math\n";
}
