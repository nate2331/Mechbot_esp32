#pragma once

#include <math.h>

namespace NavigationMath {

constexpr float PI_F = 3.14159265358979323846F;
constexpr float TWO_PI_F = 2.0F * PI_F;

inline float wrapRadians(float angle) {
  if (!isfinite(angle)) {
    return 0.0F;
  }
  while (angle > PI_F) {
    angle -= TWO_PI_F;
  }
  while (angle < -PI_F) {
    angle += TWO_PI_F;
  }
  return angle;
}

inline float boundedProportionalCorrection(float error, float gain,
                                           float maximum,
                                           float deadband) {
  error = wrapRadians(error);
  if (fabsf(error) < deadband) {
    return 0.0F;
  }

  const float correction = gain * error;
  if (correction > maximum) {
    return maximum;
  }
  if (correction < -maximum) {
    return -maximum;
  }
  return correction;
}

inline bool quaternionToYaw(float qx, float qy, float qz, float qw,
                            float &yaw) {
  const float normSquared = qx * qx + qy * qy + qz * qz + qw * qw;
  if (!isfinite(normSquared) || normSquared < 0.25F) {
    return false;
  }

  const float inverseNorm = 1.0F / sqrtf(normSquared);
  qx *= inverseNorm;
  qy *= inverseNorm;
  qz *= inverseNorm;
  qw *= inverseNorm;

  yaw = atan2f(2.0F * (qw * qz + qx * qy),
               1.0F - 2.0F * (qy * qy + qz * qz));
  return isfinite(yaw);
}

inline void fieldToRobot(float fieldForward, float fieldLeft,
                         float relativeYaw, float &robotForward,
                         float &robotLeft) {
  const float cosine = cosf(relativeYaw);
  const float sine = sinf(relativeYaw);
  robotForward = fieldForward * cosine + fieldLeft * sine;
  robotLeft = -fieldForward * sine + fieldLeft * cosine;
}

}  // namespace NavigationMath
