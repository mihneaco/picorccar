#pragma once

#include "picorccar/pico_common.h"

namespace pinout
{
using Pin = pico_common::Pin;

constexpr Pin JOYSTICK_BPRESSED_GPIO = 1;
constexpr Pin JOYSTICK_X_AXIS_GPIO  = 26;
constexpr Pin JOYSTICK_Y_AXIS_GPIO  = 27;
}
