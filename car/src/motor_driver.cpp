#include "motor_driver.h"
#include "picorccar/logger.h"

#include "pico/stdlib.h"

#include <algorithm>


MotorDriver::MotorSetpoint::MotorSetpoint(const std::int16_t p_value)
    : m_value(std::clamp(p_value,
                         static_cast<std::int16_t>(-MAX_PWM_DUTY),
                         static_cast<std::int16_t>(MAX_PWM_DUTY)))
{
    // Clamping keeps the hardware safe, but an out-of-range setpoint means a caller computed
    // a duty outside [-MAX_PWM_DUTY, MAX_PWM_DUTY]
    if (m_value != p_value)
        LOG_WARNING("clamped %d -> %d", static_cast<int>(p_value), static_cast<int>(m_value));
}

MotorDriver::MotorDriver(const pinout::DriverPins p_pins)
    : m_motor_a{"A", p_pins.m_motor_a, {}},
      m_motor_b{"B", p_pins.m_motor_b, {}},
      m_standby(p_pins.m_standby)
{
    LOG_DEBUG();
}

void MotorDriver::init()
{
    LOG_DEBUG();

    pico_common::init_gpio_pin(m_motor_a.m_pins.m_in1, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_motor_a.m_pins.m_in2, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_motor_b.m_pins.m_in1, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_motor_b.m_pins.m_in2, pico_common::GpioDirection::Output);
    pico_common::init_gpio_pin(m_standby, pico_common::GpioDirection::Output);

    pico_common::init_pwm_output_pin(m_motor_a.m_pins.m_pwm, PWM_TARGET_HZ, PWM_WRAP);
    pico_common::init_pwm_output_pin(m_motor_b.m_pins.m_pwm, PWM_TARGET_HZ, PWM_WRAP);

    stop_all();
    set_standby(true);
}

void MotorDriver::set_target(const std::int16_t p_motor_a, const std::int16_t p_motor_b)
{
    // MotorSetpoint clamps to +/-MAX_PWM_DUTY on construction, so the target is already safe.
    m_motor_a.m_state.set_target(p_motor_a);
    m_motor_b.m_state.set_target(p_motor_b);
}

void MotorDriver::service()
{
    service_motor(m_motor_a);
    service_motor(m_motor_b);
}

void MotorDriver::stop_all()
{
    LOG_DEBUG();

    // Failsafe path: zero both the applied output and the target so the next service() tick
    // cannot ramp back up toward a stale target. reset() keeps the prior applied value as
    // previous() so drive_motor still sees the mode it is transitioning away from.
    m_motor_a.m_state.reset();
    m_motor_b.m_state.reset();

    drive_motor(m_motor_a);
    drive_motor(m_motor_b);
}

void MotorDriver::set_standby(const bool p_enabled)
{
    LOG_DEBUG("STBY=%s", p_enabled ? "H" : "L");

    if (!p_enabled)
        stop_all();

    pico_common::write_gpio_output(m_standby, p_enabled);
}

void MotorDriver::service_motor(Motor& p_motor)
{
    MotorState& state = p_motor.m_state;

    // Advance the applied output one slew step toward the target, capped at PWM_SLEW_STEP.
    const std::int32_t current_value = state.current().value();
    const std::int32_t target_value = state.target().value();
    const std::int32_t next = target_value > current_value
                            ? std::min(current_value + PWM_SLEW_STEP, target_value)
                            : std::max(current_value - PWM_SLEW_STEP, target_value);

    // Force a zero crossing onto its own step so direction never flips under load: the motor
    // passes through Stop (and the reversal dead-time in drive_motor) before reversing.
    const bool crosses_zero = (current_value > 0 && next < 0) || (current_value < 0 && next > 0);
    const std::int16_t next_value = crosses_zero ? 0 : static_cast<std::int16_t>(next);

    // Skip the hardware write when nothing changed so a steady command does not re-issue
    // identical GPIO/PWM writes every tick.
    if (next_value == current_value)
        return;

    // set_current shifts the prior applied value into previous() so drive_motor can guard a
    // direction reversal.
    state.set_current(next_value);
    drive_motor(p_motor);
}

void MotorDriver::drive_motor(const Motor& p_motor)
{
    const pinout::MotorPins& pins = p_motor.m_pins;
    const DriveMode previous_mode = p_motor.m_state.previous().drive_mode();
    const DriveMode drive_mode = p_motor.m_state.current().drive_mode();
    const std::uint16_t pwm_duty = p_motor.m_state.current().duty();

    LOG_DEBUG("motor=%s mode=%s->%s duty=%u",
              p_motor.m_name,
              drive_mode_name(previous_mode),
              drive_mode_name(drive_mode),
              static_cast<uint>(pwm_duty));

    // Drop PWM before changing direction to avoid slamming directly through a reversal.
    // With slew limiting a reversal already passes through Stop, so the dead-time is a guard.
    if (drive_mode != previous_mode)
    {
        pico_common::set_pwm_output_level(pins.m_pwm, 0);
        if ((previous_mode == DriveMode::Forward
             && drive_mode == DriveMode::Reverse)
            || (previous_mode == DriveMode::Reverse
                && drive_mode == DriveMode::Forward))
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
        | Stop    | L   | L   | H   |
        +---------+-----+-----+-----+
    */
    switch (drive_mode)
    {
    case DriveMode::Forward:
        pico_common::write_gpio_output(pins.m_in1, true);
        pico_common::write_gpio_output(pins.m_in2, false);
        pico_common::set_pwm_output_level(pins.m_pwm, pwm_duty);
        break;

    case DriveMode::Reverse:
        pico_common::write_gpio_output(pins.m_in1, false);
        pico_common::write_gpio_output(pins.m_in2, true);
        pico_common::set_pwm_output_level(pins.m_pwm, pwm_duty);
        break;

    case DriveMode::Stop:
    default:
        pico_common::write_gpio_output(pins.m_in1, false);
        pico_common::write_gpio_output(pins.m_in2, false);
        // Outputs are Hi-Z (coast) regardless of PWM; level is don't-care, held high.
        pico_common::set_pwm_output_level(pins.m_pwm, PWM_FULL_DUTY);
        break;
    }
}
