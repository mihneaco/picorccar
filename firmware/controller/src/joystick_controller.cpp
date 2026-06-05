#include "joystick_controller.h"

#include "pinout.h"
#include "pico_logger.h"

#include "hardware/adc.h"

namespace
{
std::uint16_t read_adc_input(const std::uint8_t p_adc_input)
{
    adc_select_input(p_adc_input);
    return adc_read();
}
}

JoystickController::JoystickController(const pinout::JoystickControllerPins p_pins) : m_pins(p_pins)
{
    LOG_DEBUG();
}

bool JoystickController::init()
{
    LOG_DEBUG("x_axis_gpio=%u y_axis_gpio=%u",
              static_cast<uint>(m_pins.m_x_axis),
              static_cast<uint>(m_pins.m_y_axis));

    if (!pinout::is_adc_gpio(m_pins.m_x_axis) || !pinout::is_adc_gpio(m_pins.m_y_axis))
    {
        LOG_CRITICAL("Joystick ADC GPIOs must be in [%u, %u], got x=%u y=%u",
                     static_cast<uint>(pinout::ADC_GPIO_FIRST),
                     static_cast<uint>(pinout::ADC_GPIO_LAST),
                     static_cast<uint>(m_pins.m_x_axis),
                     static_cast<uint>(m_pins.m_y_axis));
        return false;
    }

    if (m_pins.m_x_axis == m_pins.m_y_axis)
    {
        LOG_CRITICAL("Joystick X/Y ADC GPIOs must be distinct, both were %u",
                     static_cast<uint>(m_pins.m_x_axis));
        return false;
    }

    adc_init();
    adc_gpio_init(m_pins.m_x_axis);
    adc_gpio_init(m_pins.m_y_axis);

    const std::uint8_t x_axis_adc_input = pinout::gpio_to_adc_input(m_pins.m_x_axis);
    const std::uint8_t y_axis_adc_input = pinout::gpio_to_adc_input(m_pins.m_y_axis);

    LOG_INFO("Joystick ADC initialized x=gpio%u/adc%u y=gpio%u/adc%u",
             static_cast<uint>(m_pins.m_x_axis),
             static_cast<unsigned>(x_axis_adc_input),
             static_cast<uint>(m_pins.m_y_axis),
             static_cast<unsigned>(y_axis_adc_input));
    return true;
}

bool JoystickController::read(Sample& p_sample) const
{
    p_sample.m_x_axis = read_adc_input(pinout::gpio_to_adc_input(m_pins.m_x_axis));
    p_sample.m_y_axis = read_adc_input(pinout::gpio_to_adc_input(m_pins.m_y_axis));
    return true;
}
