#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "../Mechbot_IMU_ESP32/NavigationMath.h"

namespace {

constexpr float TOLERANCE = 0.0001F;

void expectNear(float actual, float expected, const char *message) {
  if (std::fabs(actual - expected) > TOLERANCE) {
    std::cerr << message << ": expected " << expected << ", got " << actual
              << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using NavigationMath::PI_F;

  expectNear(NavigationMath::wrapRadians(PI_F + 0.2F), -PI_F + 0.2F,
             "positive wraparound");
  expectNear(NavigationMath::wrapRadians(-PI_F - 0.2F), PI_F - 0.2F,
             "negative wraparound");
  expectNear(NavigationMath::wrapRadians(
                 std::numeric_limits<float>::infinity()),
             0.0F, "non-finite wraparound input");

  expectNear(NavigationMath::boundedProportionalCorrection(
                 0.01F, 0.7F, 0.3F, 0.02F),
             0.0F, "heading deadband");
  expectNear(NavigationMath::boundedProportionalCorrection(
                 1.0F, 0.7F, 0.3F, 0.02F),
             0.3F, "positive correction limit");
  expectNear(NavigationMath::boundedProportionalCorrection(
                 -1.0F, 0.7F, 0.3F, 0.02F),
             -0.3F, "negative correction limit");
  expectNear(NavigationMath::boundedProportionalCorrection(
                 -2.0F * PI_F + 0.1F, 0.7F, 0.3F, 0.02F),
             0.07F, "correction wraparound");

  float yaw = 0.0F;
  if (!NavigationMath::quaternionToYaw(0.0F, 0.0F,
                                       std::sin(PI_F / 4.0F),
                                       std::cos(PI_F / 4.0F), yaw)) {
    std::cerr << "valid quaternion rejected\n";
    return 1;
  }
  expectNear(yaw, PI_F / 2.0F, "quaternion yaw");

  if (NavigationMath::quaternionToYaw(0.0F, 0.0F, 0.0F, 0.0F, yaw)) {
    std::cerr << "invalid quaternion accepted\n";
    return 1;
  }

  float robotForward = 0.0F;
  float robotLeft = 0.0F;
  NavigationMath::fieldToRobot(1.0F, 0.0F, PI_F / 2.0F,
                               robotForward, robotLeft);
  expectNear(robotForward, 0.0F, "field forward at +90 degrees: forward");
  expectNear(robotLeft, -1.0F, "field forward at +90 degrees: left");

  NavigationMath::fieldToRobot(0.0F, 1.0F, -PI_F / 2.0F,
                               robotForward, robotLeft);
  expectNear(robotForward, -1.0F, "field left at -90 degrees: forward");
  expectNear(robotLeft, 0.0F, "field left at -90 degrees: left");

  // A left stick 15 degrees left of forward must retain both components.
  const float forward15 = std::cos(15.0F * PI_F / 180.0F);
  const float left15 = std::sin(15.0F * PI_F / 180.0F);
  NavigationMath::fieldToRobot(forward15, left15, PI_F / 6.0F,
                               robotForward, robotLeft);
  expectNear(robotForward, forward15, "field-oriented analog vector: forward");
  expectNear(robotLeft, -left15, "field-oriented analog vector: left");

  float wheel[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  NavigationMath::mecanumMix(forward15, left15, 0.0F, wheel);
  expectNear(wheel[0], 0.577350F, "15 degree translation: FL");
  expectNear(wheel[1], 1.0F, "15 degree translation: FR");
  expectNear(wheel[2], 1.0F, "15 degree translation: RL");
  expectNear(wheel[3], 0.577350F, "15 degree translation: RR");

  // Right-stick rotation remains mixed with the same two-axis translation.
  NavigationMath::mecanumMix(forward15, left15, 0.30F, wheel);
  expectNear(wheel[0], 0.266999F, "mixed translation and turn: FL");
  expectNear(wheel[1], 1.0F, "mixed translation and turn: FR");
  expectNear(wheel[2], 0.606491F, "mixed translation and turn: RL");
  expectNear(wheel[3], 0.660508F, "mixed translation and turn: RR");

  std::cout << "navigation math tests passed\n";
  return 0;
}
