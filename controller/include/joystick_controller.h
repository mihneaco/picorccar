#pragma once

#include <cstdint>

#include "pinout.h"

namespace pinout
{
    struct JoystickControllerPins
    {
        Pin m_bpressed{};
        Pin m_x_axis{};
        Pin m_y_axis{};
    };

    constexpr JoystickControllerPins JOYSTICK_PINS
    {
        pinout::JOYSTICK_BPRESSED_GPIO,
        pinout::JOYSTICK_X_AXIS_GPIO,
        pinout::JOYSTICK_Y_AXIS_GPIO
    };
}

class JoystickController
{
public:
    struct Sample
    {
        bool          m_bpressed{};
        std::uint16_t m_x_axis{};
        std::uint16_t m_y_axis{};
    };

    static constexpr std::uint8_t ADC_INPUT_BITS = 12;
    static constexpr std::uint16_t ADC_MAX_VALUE = (1u << ADC_INPUT_BITS) - 1u;

    explicit JoystickController(pinout::JoystickControllerPins p_pins);

    bool init();
    bool read(Sample& p_sample) const;

private:
    pinout::JoystickControllerPins m_pins;
    bool m_initialized = false;
};
