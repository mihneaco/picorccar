#include "joystick_controller.h"
#include "pinout.h"

#include "picorccar/logger.h"

#include <algorithm>

namespace
{
constexpr bool JOYSTICK_BUTTON_PRESSED_LEVEL = false;
#ifdef PICORCCAR_DEBUG
constexpr std::uint8_t DEBUG_SAMPLE_LOG_PERIOD = 50;
#endif
}

JoystickController::Sample::Sample(const bool p_bpressed,
                                   const std::uint16_t p_x_axis,
                                   const std::uint16_t p_y_axis)
    : m_bpressed(p_bpressed),
      // Axes are bounded to the ADC range so every consumer can rely on it.
      m_x_axis(std::min(p_x_axis, ADC_MAX_VALUE)),
      m_y_axis(std::min(p_y_axis, ADC_MAX_VALUE))
{
}

std::uint16_t JoystickController::Sample::max_center_offset() const
{
    const auto axis_offset = [] (const std::uint16_t p_value) -> std::uint16_t {
        return p_value > ADC_CENTER ? p_value - ADC_CENTER
                                    : ADC_CENTER - p_value;
    };
    return std::max(axis_offset(m_x_axis), axis_offset(m_y_axis));
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

std::optional<JoystickController::Sample> JoystickController::read() const
{
    if (!m_initialized)
    {
        LOG_WARNING("Joystick read requested before initialization");
        return std::nullopt;
    }

    const Sample sample{
        pico_common::read_gpio_input(m_pins.m_bpressed) == JOYSTICK_BUTTON_PRESSED_LEVEL,
        pico_common::read_adc_input(pico_common::gpio_to_adc_input(m_pins.m_x_axis)),
        pico_common::read_adc_input(pico_common::gpio_to_adc_input(m_pins.m_y_axis))};

#ifdef PICORCCAR_DEBUG
    static std::uint8_t sample_log_count = 0;
    if (++sample_log_count == DEBUG_SAMPLE_LOG_PERIOD)
    {
        sample_log_count = 0;
        LOG_DEBUG("sample.m_bpressed: %u; sample.m_x_axis: %u; sample.m_y_axis: %u",
                  sample.m_bpressed,
                  sample.m_x_axis,
                  sample.m_y_axis);
    }
#endif

    return sample;
}
