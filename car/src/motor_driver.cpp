#include "motor_driver.h"
#include "picorccar/logger.h"

#include "pico/stdlib.h"

#include <algorithm>
#include <cstdlib>

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

MotorDriver::MotorCommand::MotorCommand(const std::int32_t p_value)
    : m_value(std::clamp(p_value,
                         -static_cast<std::int32_t>(MAX_PWM_DUTY),
                         static_cast<std::int32_t>(MAX_PWM_DUTY)))
{
}

MotorDriver::DriveMode MotorDriver::MotorCommand::drive_mode() const
{
    if (m_value > 0)
        return DriveMode::Forward;
    if (m_value < 0)
        return DriveMode::Reverse;
    return DriveMode::Stop;
}

std::uint16_t MotorDriver::MotorCommand::duty() const
{
    const std::int32_t magnitude = std::abs(m_value);
    return static_cast<std::uint16_t>(std::min(magnitude, static_cast<std::int32_t>(PWM_WRAP)));
}

void MotorDriver::MotorState::slew_toward_target()
{
    const std::int32_t current = m_current.value();
    const std::int32_t target = m_target.value();
    const std::int32_t next = target > current
                            ? std::min(current + PWM_SLEW_STEP, target)
                            : std::max(current - PWM_SLEW_STEP, target);

    // Force a zero crossing onto its own step so direction never flips under load: the motor
    // passes through Stop (and the reversal dead-time in drive_motor) before reversing.
    if ((current > 0 && next < 0) || (current < 0 && next > 0))
        m_current = MotorCommand{0};
    else
        m_current = MotorCommand{next};
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

void MotorDriver::set_target(const DriverCommand p_command)
{
    // MotorCommand clamps to +/-MAX_PWM_DUTY on construction, so the target is already safe.
    m_motor_state_a.m_target = p_command.m_motor_a;
    m_motor_state_b.m_target = p_command.m_motor_b;
}

void MotorDriver::service()
{
    service_motor(m_pins.m_motor_a, m_motor_state_a);
    service_motor(m_pins.m_motor_b, m_motor_state_b);
}

void MotorDriver::stop_all()
{
    LOG_DEBUG();

    const DriveMode previous_mode_a = m_motor_state_a.m_current.drive_mode();
    const DriveMode previous_mode_b = m_motor_state_b.m_current.drive_mode();

    m_motor_state_a = MotorState{};
    m_motor_state_b = MotorState{};

    drive_motor(m_pins.m_motor_a, previous_mode_a, DriveMode::Stop, 0);
    drive_motor(m_pins.m_motor_b, previous_mode_b, DriveMode::Stop, 0);
}

void MotorDriver::set_standby(const bool p_enabled)
{
    LOG_DEBUG("STBY=%s", p_enabled ? "H" : "L");

    if (!p_enabled)
        stop_all();

    pico_common::write_gpio_output(m_pins.m_standby, p_enabled);
}

void MotorDriver::service_motor(const pinout::MotorPins& p_pins, MotorState& p_motor)
{
    const MotorCommand previous = p_motor.m_current;
    p_motor.slew_toward_target();

    // Skip the hardware write when nothing changed so a steady command does not re-issue
    // identical GPIO/PWM writes every tick.
    if (p_motor.m_current.value() == previous.value())
        return;

    drive_motor(p_pins,
                previous.drive_mode(),
                p_motor.m_current.drive_mode(),
                p_motor.m_current.duty());
}

bool MotorDriver::is_drive_mode_reversal(const DriveMode p_current, const DriveMode p_next)
{
    return (p_current == DriveMode::Forward && p_next == DriveMode::Reverse) ||
           (p_current == DriveMode::Reverse && p_next == DriveMode::Forward);
}

void MotorDriver::drive_motor(const pinout::MotorPins& p_pins,
                              const DriveMode p_previous_mode,
                              const DriveMode p_drive_mode,
                              const std::uint16_t p_pwm_duty)
{
    LOG_DEBUG("pins=%u/%u/%u mode=%s->%s duty=%u",
              static_cast<uint>(p_pins.m_in1),
              static_cast<uint>(p_pins.m_in2),
              static_cast<uint>(p_pins.m_pwm),
              drive_mode_name(p_previous_mode),
              drive_mode_name(p_drive_mode),
              static_cast<uint>(p_pwm_duty));

    // Drop PWM before changing direction to avoid slamming directly through a reversal.
    // With slew limiting a reversal already passes through Stop, so the dead-time is a guard.
    if (p_drive_mode != p_previous_mode)
    {
        pico_common::set_pwm_output_level(p_pins.m_pwm, 0);
        if (is_drive_mode_reversal(p_previous_mode, p_drive_mode))
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
    switch (p_drive_mode)
    {
    case DriveMode::Forward:
        pico_common::write_gpio_output(p_pins.m_in1, true);
        pico_common::write_gpio_output(p_pins.m_in2, false);
        pico_common::set_pwm_output_level(p_pins.m_pwm, p_pwm_duty);
        break;

    case DriveMode::Reverse:
        pico_common::write_gpio_output(p_pins.m_in1, false);
        pico_common::write_gpio_output(p_pins.m_in2, true);
        pico_common::set_pwm_output_level(p_pins.m_pwm, p_pwm_duty);
        break;

    case DriveMode::Brake:
        pico_common::write_gpio_output(p_pins.m_in1, true);
        pico_common::write_gpio_output(p_pins.m_in2, true);
        // Outputs are driven by IN pins; PWM high keeps the brake engaged.
        pico_common::set_pwm_output_level(p_pins.m_pwm, PWM_FULL_DUTY);
        break;

    case DriveMode::Stop:
    default:
        pico_common::write_gpio_output(p_pins.m_in1, false);
        pico_common::write_gpio_output(p_pins.m_in2, false);
        // Outputs are Hi-Z (coast) regardless of PWM; level is don't-care, held high.
        pico_common::set_pwm_output_level(p_pins.m_pwm, PWM_FULL_DUTY);
        break;
    }
}
