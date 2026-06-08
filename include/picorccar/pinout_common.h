#pragma once

#include <cstdint>

#include "pico/types.h"

namespace pinout
{
using Pin = uint;

constexpr Pin ADC_GPIO_FIRST = 26;
constexpr Pin ADC_GPIO_LAST = 29;

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

constexpr bool is_adc_gpio(const Pin p_pin)
{
    return p_pin >= ADC_GPIO_FIRST && p_pin <= ADC_GPIO_LAST;
}

constexpr std::uint8_t gpio_to_adc_input(const Pin p_pin)
{
    for (const auto& map_entry : ADC_GPIO_MAP)
    {
        if (map_entry.m_gpio == p_pin)
            return map_entry.m_adc_input;
    }

    return 0;
}
}
