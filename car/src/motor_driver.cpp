#include "motor_driver.h"
#include "picorccar/pico_logger.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

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

    init_pins();

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

void MotorDriver::set_standby(const bool p_enabled)
{
    LOG_DEBUG("STBY=%s", p_enabled ? "H" : "L");

    if (!p_enabled)
        stop_all();

    gpio_put(m_pins.m_standby, p_enabled ? 1 : 0);
}

void MotorDriver::stop_all()
{
    LOG_DEBUG();

    set_motor_a(DriveMode::Stop, 0);
    set_motor_b(DriveMode::Stop, 0);
}

void MotorDriver::init_pins()
{
    LOG_DEBUG();

    init_control_pin(m_pins.m_motor_a.m_in1);
    init_control_pin(m_pins.m_motor_a.m_in2);
    init_control_pin(m_pins.m_motor_b.m_in1);
    init_control_pin(m_pins.m_motor_b.m_in2);
    init_control_pin(m_pins.m_standby);

    init_pwm_pin(m_pins.m_motor_a.m_pwm);
    init_pwm_pin(m_pins.m_motor_b.m_pwm);
}

void MotorDriver::init_control_pin(const pinout::Pin p_pin)
{
    LOG_DEBUG("pin=%u", static_cast<uint>(p_pin));

    gpio_init(p_pin);
    gpio_set_dir(p_pin, GPIO_OUT);
}

void MotorDriver::init_pwm_pin(const pinout::Pin p_pwm_pin)
{
    LOG_DEBUG("pin=%u", static_cast<uint>(p_pwm_pin));

    gpio_set_function(p_pwm_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(p_pwm_pin);

    pwm_config config = pwm_get_default_config();
    float clkdiv = clock_get_hz(clk_sys) / (PWM_TARGET_HZ * (PWM_WRAP + 1.f));
    if (clkdiv < PWM_CLKDIV_MIN)
        clkdiv = PWM_CLKDIV_MIN;
    else if (clkdiv > PWM_CLKDIV_MAX)
        clkdiv = PWM_CLKDIV_MAX;

    pwm_config_set_clkdiv(&config, clkdiv);
    pwm_config_set_wrap(&config, PWM_WRAP);
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(p_pwm_pin, 0);
}

void MotorDriver::set_pwm_duty(const pinout::Pin p_pwm_pin, const std::uint16_t p_duty)
{
    LOG_TRACE("pin=%u duty=%u",
              static_cast<uint>(p_pwm_pin),
              static_cast<uint>(p_duty));

    assert(is_known_pwm_pin(p_pwm_pin));

    const std::uint16_t clamped_duty = p_duty > PWM_FULL_DUTY ? PWM_FULL_DUTY : p_duty;
    pwm_set_gpio_level(p_pwm_pin, clamped_duty);
}

bool MotorDriver::is_known_pwm_pin(const pinout::Pin p_pwm_pin) const
{
    return p_pwm_pin == m_pins.m_motor_a.m_pwm || p_pwm_pin == m_pins.m_motor_b.m_pwm;
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
    const std::uint16_t clamped_speed = p_pwm_duty > PWM_WRAP ? PWM_WRAP : p_pwm_duty;

    // Drop PWM before changing direction to avoid slamming directly through a reversal.
    if (p_drive_mode != p_motor.m_drive_mode)
    {
        set_pwm_duty(p_pins.m_pwm, 0);
        if (is_drive_mode_reversal(p_motor.m_drive_mode, p_drive_mode))
            sleep_us(DIRECTION_CHANGE_DEADTIME_US);
    }

    std::uint16_t applied_pwm_duty = PWM_FULL_DUTY;

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
        gpio_put(p_pins.m_in1, 1);
        gpio_put(p_pins.m_in2, 0);
        applied_pwm_duty = clamped_speed;
        set_pwm_duty(p_pins.m_pwm, applied_pwm_duty);
        break;

    case DriveMode::Reverse:
        gpio_put(p_pins.m_in1, 0);
        gpio_put(p_pins.m_in2, 1);
        applied_pwm_duty = clamped_speed;
        set_pwm_duty(p_pins.m_pwm, applied_pwm_duty);
        break;

    case DriveMode::Brake:
        gpio_put(p_pins.m_in1, 1);
        gpio_put(p_pins.m_in2, 1);
        set_pwm_duty(p_pins.m_pwm, applied_pwm_duty);
        break;

    case DriveMode::Stop:
    default:
        gpio_put(p_pins.m_in1, 0);
        gpio_put(p_pins.m_in2, 0);
        set_pwm_duty(p_pins.m_pwm, applied_pwm_duty);
        break;
    }

    p_motor.m_drive_mode = p_drive_mode;
    p_motor.m_pwm_duty = applied_pwm_duty;
}
