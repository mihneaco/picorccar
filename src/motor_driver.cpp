#include "motor_driver.h"
#include "pico_logger.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

namespace
{
constexpr const char* direction_name(const MotorDriver::Direction p_direction)
{
    switch (p_direction)
    {
    case MotorDriver::Direction::Stop:
        return "Stop";
    case MotorDriver::Direction::Forward:
        return "Forward";
    case MotorDriver::Direction::Reverse:
        return "Reverse";
    case MotorDriver::Direction::Brake:
        return "Brake";
    default:
        return "Unknown";
    }
}
}

MotorDriver::MotorDriver(const DriverPins p_pins) : m_pins(p_pins)
{
    LOG_DEBUG();
    gpio_put(m_pins.m_standby, 0);
}

void MotorDriver::init()
{
    LOG_DEBUG();

    init_pins();

    stop_all();
    gpio_put(m_pins.m_standby, 1);
}

void MotorDriver::set_motor_a(const Direction p_direction, const std::uint16_t p_speed)
{
    LOG_DEBUG();

    set_motor(m_pins.m_motor_a, m_motor_state_a, p_direction, p_speed);
}

void MotorDriver::set_motor_b(const Direction p_direction, const std::uint16_t p_speed)
{
    LOG_DEBUG();

    set_motor(m_pins.m_motor_b, m_motor_state_b, p_direction, p_speed);
}

void MotorDriver::stop_all()
{
    LOG_DEBUG();

    set_motor_a(Direction::Stop, 0);
    set_motor_b(Direction::Stop, 0);
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

void MotorDriver::init_control_pin(const Pin p_pin)
{
    LOG_DEBUG();

    gpio_init(p_pin);
    gpio_set_dir(p_pin, GPIO_OUT);
}

void MotorDriver::init_pwm_pin(const Pin p_pwm_pin)
{
    LOG_DEBUG();

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

void MotorDriver::set_pwm_duty(const Pin p_pwm_pin, const std::uint16_t p_duty)
{
    LOG_TRACE();

    assert(is_known_pwm_pin(p_pwm_pin));

    const std::uint16_t clamped_duty = p_duty > PWM_FULL_DUTY ? PWM_FULL_DUTY : p_duty;
    pwm_set_gpio_level(p_pwm_pin, clamped_duty);
}

bool MotorDriver::is_known_pwm_pin(const Pin p_pwm_pin) const
{
    return p_pwm_pin == m_pins.m_motor_a.m_pwm || p_pwm_pin == m_pins.m_motor_b.m_pwm;
}

bool MotorDriver::is_direction_reversal(const Direction p_current, const Direction p_next)
{
    return (p_current == Direction::Forward && p_next == Direction::Reverse) ||
           (p_current == Direction::Reverse && p_next == Direction::Forward);
}

void MotorDriver::set_motor(const MotorPins& p_pins,
                            MotorState& p_motor,
                            const Direction p_direction,
                            const std::uint16_t p_speed)
{
    LOG_DEBUG("pins=%u/%u/%u state=%s/%u request=%s/%u",
              static_cast<uint>(p_pins.m_in1),
              static_cast<uint>(p_pins.m_in2),
              static_cast<uint>(p_pins.m_pwm),
              direction_name(p_motor.m_direction),
              static_cast<uint>(p_motor.m_speed),
              direction_name(p_direction),
              static_cast<uint>(p_speed));

    if (p_speed > PWM_WRAP)
        LOG_WARNING("speed > PWM_WRAP, defaulting to PWM_WRAP");
    const std::uint16_t clamped_speed = p_speed > PWM_WRAP ? PWM_WRAP : p_speed;

    // Drop PWM before changing direction to avoid slamming directly through a reversal.
    if (p_direction != p_motor.m_direction)
    {
        set_pwm_duty(p_pins.m_pwm, 0);
        if (is_direction_reversal(p_motor.m_direction, p_direction))
            sleep_us(DIRECTION_CHANGE_DEADTIME_US);
    }

    /*
        TB6612 direction table with STBY=H. Forward/reverse assume the current
        wiring polarity; swap the labels if motor leads are reversed.
        
        +---------+-----+-----+-----+
        | State   | IN1 | IN2 | PWM |
        +---------+-----+-----+-----+
        | Forward | H   | L   | PWM |
        | Reverse | L   | H   | PWM |
        | Brake   | H   | H   | H/L |
        | Stop    | L   | L   | H   |
        +---------+-----+-----+-----+
    */
    switch (p_direction)
    {
    case Direction::Forward:
        gpio_put(p_pins.m_in1, 1);
        gpio_put(p_pins.m_in2, 0);
        set_pwm_duty(p_pins.m_pwm, clamped_speed);
        break;

    case Direction::Reverse:
        gpio_put(p_pins.m_in1, 0);
        gpio_put(p_pins.m_in2, 1);
        set_pwm_duty(p_pins.m_pwm, clamped_speed);
        break;

    case Direction::Brake:
        gpio_put(p_pins.m_in1, 1);
        gpio_put(p_pins.m_in2, 1);
        set_pwm_duty(p_pins.m_pwm, PWM_FULL_DUTY);
        break;

    case Direction::Stop:
    default:
        gpio_put(p_pins.m_in1, 0);
        gpio_put(p_pins.m_in2, 0);
        set_pwm_duty(p_pins.m_pwm, PWM_FULL_DUTY);
        break;
    }

    p_motor.m_direction = p_direction;
    p_motor.m_speed = clamped_speed;
}
