#include "motor_driver.h"
#include "picorccar/logger.h"

#include "pico/stdlib.h"

#include <algorithm>

namespace
{
constexpr const char* drive_mode_name(const MotorDriver::DriveMode p_drive_mode)
{
    switch (p_drive_mode)
    {
    case MotorDriver::DriveMode::Stop:
        return "Stop";
    case MotorDriver::DriveMode::Forward:
        return "Forward";
    case MotorDriver::DriveMode::Reverse:
        return "Reverse";
    case MotorDriver::DriveMode::Brake:
        return "Brake";
    default:
        return "Unknown";
    }
}
}

MotorDriver::MotorDriver(const pinout::DriverPins p_pins) : m_pins(p_pins)
{
    LOG_DEBUG();
}

void MotorDriver::init()
{
    LOG_DEBUG();

    pico_common::init_gpio_pin(m_pins.m_motor_a.m_in1, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_pins.m_motor_a.m_in2, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_pins.m_motor_b.m_in1, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_pins.m_motor_b.m_in2, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_pins.m_standby, pico_common::GpioDirection::Output);

    pico_common::init_pwm_output_pin(m_pins.m_motor_a.m_pwm, PWM_TARGET_HZ, PWM_WRAP);
    pico_common::init_pwm_output_pin(m_pins.m_motor_b.m_pwm, PWM_TARGET_HZ, PWM_WRAP);

    stop_all();
    set_standby(true);
}

void MotorDriver::set_motor_a(const DriveMode p_drive_mode, const std::uint16_t p_pwm_duty)
{
    set_motor(m_pins.m_motor_a, m_motor_state_a, p_drive_mode, p_pwm_duty);
}

void MotorDriver::set_motor_b(const DriveMode p_drive_mode, const std::uint16_t p_pwm_duty)
{
    set_motor(m_pins.m_motor_b, m_motor_state_b, p_drive_mode, p_pwm_duty);
}

void MotorDriver::stop_all()
{
    LOG_DEBUG();

    set_motor_a(DriveMode::Stop, 0);
    set_motor_b(DriveMode::Stop, 0);
}

void MotorDriver::set_standby(const bool p_enabled)
{
    LOG_DEBUG("STBY=%s", p_enabled ? "H" : "L");

    if (!p_enabled)
        stop_all();

    pico_common::write_gpio_output(m_pins.m_standby, p_enabled);
}

bool MotorDriver::is_drive_mode_reversal(const DriveMode p_current, const DriveMode p_next)
{
    return (p_current == DriveMode::Forward && p_next == DriveMode::Reverse) ||
           (p_current == DriveMode::Reverse && p_next == DriveMode::Forward);
}

void MotorDriver::set_motor(const pinout::MotorPins& p_pins,
                            MotorState& p_motor,
                            const DriveMode p_drive_mode,
                            const std::uint16_t p_pwm_duty)
{
    LOG_DEBUG("pins=%u/%u/%u state=%s/%u request=%s/%u",
              static_cast<uint>(p_pins.m_in1),
              static_cast<uint>(p_pins.m_in2),
              static_cast<uint>(p_pins.m_pwm),
              drive_mode_name(p_motor.m_drive_mode),
              static_cast<uint>(p_motor.m_pwm_duty),
              drive_mode_name(p_drive_mode),
              static_cast<uint>(p_pwm_duty));

    if (p_pwm_duty > PWM_WRAP)
        LOG_WARNING("speed > PWM_WRAP, defaulting to PWM_WRAP");
    const std::uint16_t clamped_speed = std::min(p_pwm_duty, PWM_WRAP);

    // Drop PWM before changing direction to avoid slamming directly through a reversal.
    if (p_drive_mode != p_motor.m_drive_mode)
    {
        pico_common::set_pwm_output_level(p_pins.m_pwm, 0);
        if (is_drive_mode_reversal(p_motor.m_drive_mode, p_drive_mode))
            sleep_us(DIRECTION_CHANGE_DEADTIME_US);
    }

    /*
        TB6612 direction table with STBY=H. Forward/reverse assume the current
        wiring polarity; swap the labels if motor wires are reversed.
        
        +---------+-----+-----+-----+
        | State   | IN1 | IN2 | PWM |
        +---------+-----+-----+-----+
        | Forward | H   | L   | PWM |
        | Reverse | L   | H   | PWM |
        | Brake   | H   | H   | H/L |
        | Stop    | L   | L   | H   |
        +---------+-----+-----+-----+
    */
    std::uint16_t applied_pwm_duty = PWM_FULL_DUTY;
    switch (p_drive_mode)
    {
    case DriveMode::Forward:
        pico_common::write_gpio_output(p_pins.m_in1, true);
        pico_common::write_gpio_output(p_pins.m_in2, false);
        applied_pwm_duty = clamped_speed;
        pico_common::set_pwm_output_level(p_pins.m_pwm, applied_pwm_duty);
        break;

    case DriveMode::Reverse:
        pico_common::write_gpio_output(p_pins.m_in1, false);
        pico_common::write_gpio_output(p_pins.m_in2, true);
        applied_pwm_duty = clamped_speed;
        pico_common::set_pwm_output_level(p_pins.m_pwm, applied_pwm_duty);
        break;

    case DriveMode::Brake:
        pico_common::write_gpio_output(p_pins.m_in1, true);
        pico_common::write_gpio_output(p_pins.m_in2, true);
        pico_common::set_pwm_output_level(p_pins.m_pwm, applied_pwm_duty);
        break;

    case DriveMode::Stop:
    default:
        pico_common::write_gpio_output(p_pins.m_in1, false);
        pico_common::write_gpio_output(p_pins.m_in2, false);
        pico_common::set_pwm_output_level(p_pins.m_pwm, applied_pwm_duty);
        break;
    }

    p_motor.m_drive_mode = p_drive_mode;
    p_motor.m_pwm_duty = applied_pwm_duty;
}
