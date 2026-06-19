#include "joystick_controller.h"
#include "pinout.h"

#include "picorccar/logger.h"

namespace
{
constexpr bool JOYSTICK_BUTTON_PRESSED_LEVEL = false;
#ifdef PICORCCAR_DEBUG
constexpr std::uint8_t DEBUG_SAMPLE_LOG_PERIOD = 50;
#endif
}

JoystickController::JoystickController(const pinout::JoystickControllerPins p_pins) : m_pins(p_pins)
{
    LOG_DEBUG();
}

bool JoystickController::init()
{
    LOG_DEBUG("bpressed_gpio=%u x_axis_gpio=%u y_axis_gpio=%u",
              static_cast<uint>(m_pins.m_bpressed),
              static_cast<uint>(m_pins.m_x_axis),
              static_cast<uint>(m_pins.m_y_axis));

    if (!pico_common::is_adc_gpio(m_pins.m_x_axis) || !pico_common::is_adc_gpio(m_pins.m_y_axis))
    {
        LOG_CRITICAL("Joystick ADC GPIOs must be in [%u, %u], got x=%u y=%u",
                     static_cast<uint>(pico_common::ADC_GPIO_FIRST),
                     static_cast<uint>(pico_common::ADC_GPIO_LAST),
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

    // Joystick button is active low
    pico_common::init_gpio_pin(m_pins.m_bpressed,
                               pico_common::GpioDirection::Input,
                               pico_common::GpioPullMode::PullUp);

    pico_common::init_adc();
    pico_common::init_adc_gpio_pin(m_pins.m_x_axis);
    pico_common::init_adc_gpio_pin(m_pins.m_y_axis);
    m_initialized = true;

    const std::uint8_t x_axis_adc_input = pico_common::gpio_to_adc_input(m_pins.m_x_axis);
    const std::uint8_t y_axis_adc_input = pico_common::gpio_to_adc_input(m_pins.m_y_axis);

    LOG_INFO("Joystick initialized button=gpio%u x=gpio%u/adc%u y=gpio%u/adc%u",
             static_cast<uint>(m_pins.m_bpressed),
             static_cast<uint>(m_pins.m_x_axis),
             static_cast<unsigned>(x_axis_adc_input),
             static_cast<uint>(m_pins.m_y_axis),
             static_cast<unsigned>(y_axis_adc_input));
    return true;
}

bool JoystickController::read(Sample& p_sample) const
{
    if (!m_initialized)
    {
        LOG_WARNING("Joystick read requested before initialization");
        p_sample = {};
        return false;
    }

    p_sample.m_bpressed = pico_common::read_gpio_input(m_pins.m_bpressed) == JOYSTICK_BUTTON_PRESSED_LEVEL;
    p_sample.m_x_axis = pico_common::read_adc_input(pico_common::gpio_to_adc_input(m_pins.m_x_axis));
    p_sample.m_y_axis = pico_common::read_adc_input(pico_common::gpio_to_adc_input(m_pins.m_y_axis));

#ifdef PICORCCAR_DEBUG
    static std::uint8_t sample_log_count = 0;
    if (++sample_log_count == DEBUG_SAMPLE_LOG_PERIOD)
    {
        sample_log_count = 0;
        LOG_DEBUG("p_sample.m_bpressed: %u; p_sample.m_x_axis: %u; p_sample.m_y_axis: %u",
                  p_sample.m_bpressed,
                  p_sample.m_x_axis,
                  p_sample.m_y_axis);
    }
#endif

    return true;
}
