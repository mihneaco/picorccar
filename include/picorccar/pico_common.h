#pragma once

#include <cstdint>

#include "pico/types.h"

#include "hardware/gpio.h"

#if __has_include("hardware/adc.h")
#include "hardware/adc.h"
#endif

#if __has_include("hardware/clocks.h")
#include "hardware/clocks.h"
#endif

#if __has_include("hardware/pwm.h")
#include "hardware/pwm.h"
#endif

namespace pico_common
{
// uint is overkill but its what the pico sdk uses. Using it for convenience.
using Pin = uint;

enum class GpioDirection
{
    Input,
    Output
};

enum class GpioPullMode
{
    None,
    PullUp,
    PullDown
};

constexpr Pin ADC_GPIO_FIRST = 26;
constexpr Pin ADC_GPIO_LAST = 29;
constexpr bool is_adc_gpio(const Pin p_pin)
{
    return p_pin >= ADC_GPIO_FIRST && p_pin <= ADC_GPIO_LAST;
}

struct AdcPinMapEntry
{
    Pin m_gpio;
    std::uint8_t m_adc_input;
};
constexpr AdcPinMapEntry ADC_GPIO_MAP[] = {
    {26, 0},
    {27, 1},
    {28, 2},
    {29, 3}
};
constexpr std::uint8_t gpio_to_adc_input(const Pin p_pin)
{
    for (const auto& map_entry : ADC_GPIO_MAP)
    {
        if (map_entry.m_gpio == p_pin)
            return map_entry.m_adc_input;
    }

    return 0;
}

inline void init_gpio_pin(const Pin p_pin,
                          const GpioDirection p_direction,
                          const GpioPullMode p_pull_mode = GpioPullMode::None)
{
    gpio_init(p_pin);
    gpio_set_dir(p_pin, p_direction == GpioDirection::Output ? GPIO_OUT : GPIO_IN);

    switch (p_pull_mode)
    {
    case GpioPullMode::PullUp:
        gpio_pull_up(p_pin);
        break;
    case GpioPullMode::PullDown:
        gpio_pull_down(p_pin);
        break;
    case GpioPullMode::None:
    default:
        break;
    }
}

inline bool read_gpio_input(const Pin p_pin)
{
    return gpio_get(p_pin);
}

inline void write_gpio_output(const Pin p_pin, const bool p_level)
{
    gpio_put(p_pin, p_level ? 1 : 0);
}

#if __has_include("hardware/adc.h")
inline void init_adc()
{
    adc_init();
}

inline void init_adc_gpio_pin(const Pin p_pin)
{
    adc_gpio_init(p_pin);
}

inline std::uint16_t read_adc_input(const std::uint8_t p_adc_input)
{
    adc_select_input(p_adc_input);
    return adc_read();
}
#endif

#if __has_include("hardware/clocks.h") && __has_include("hardware/pwm.h")
inline void init_pwm_output_pin(const Pin p_pin,
                                const std::uint32_t p_target_hz,
                                const std::uint16_t p_wrap,
                                const std::uint16_t p_initial_level = 0)
{
    constexpr float PWM_CLKDIV_MIN = 1.0f;
    // Pico PWM clock divider is 8.4 fixed-point, so the largest value is 255 + 15/16.
    constexpr float PWM_CLKDIV_MAX = 255.0f + (15.0f / 16.0f);

    gpio_set_function(p_pin, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(p_pin);

    pwm_config config = pwm_get_default_config();
    // In free-running mode: pwm_hz = clk_sys / (clkdiv * (wrap + 1)).
    float clkdiv = static_cast<float>(clock_get_hz(clk_sys)) /
                   (static_cast<float>(p_target_hz) * (static_cast<float>(p_wrap) + 1.0f));
    if (clkdiv < PWM_CLKDIV_MIN)
        clkdiv = PWM_CLKDIV_MIN;
    else if (clkdiv > PWM_CLKDIV_MAX)
        clkdiv = PWM_CLKDIV_MAX;

    pwm_config_set_clkdiv(&config, clkdiv);
    pwm_config_set_wrap(&config, p_wrap);
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(p_pin, p_initial_level);
}

inline void set_pwm_output_level(const Pin p_pin, const std::uint16_t p_level)
{
    pwm_set_gpio_level(p_pin, p_level);
}
#endif

}
