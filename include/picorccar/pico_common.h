#pragma once

#include <cstdint>

#include "pico/types.h"

#include "hardware/gpio.h"

#if __has_include("hardware/adc.h")
#include "hardware/adc.h"
#endif

namespace pico_common
{
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

}
