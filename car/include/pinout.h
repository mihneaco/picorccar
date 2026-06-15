#pragma once

#include "picorccar/pico_common.h"

namespace pinout
{
using Pin = pico_common::Pin;

constexpr Pin MOTOR_A_PWM = 2;
constexpr Pin MOTOR_A_IN2 = 3;
constexpr Pin MOTOR_A_IN1 = 4;
constexpr Pin MOTOR_DRIVER_STANDBY = 5;
constexpr Pin MOTOR_B_IN1 = 6;
constexpr Pin MOTOR_B_IN2 = 7;
constexpr Pin MOTOR_B_PWM = 8;
}
