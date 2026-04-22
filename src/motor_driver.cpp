#include "motor_driver.h"
#include "pico_logger.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

namespace
{
constexpr const char* direction_name(const MotorDriver::Direction direction)
{
    switch (direction)
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

MotorDriver::MotorDriver(const DriverPins pins) : m_pins(pins)
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

void MotorDriver::set_motor_a(const Direction direction, const std::uint16_t speed)
{
    LOG_DEBUG();

    set_motor(m_pins.m_motor_a, m_motor_a, direction, speed);
}

void MotorDriver::set_motor_b(const Direction direction, const std::uint16_t speed)
{
    LOG_DEBUG();

    set_motor(m_pins.m_motor_b, m_motor_b, direction, speed);
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

void MotorDriver::init_control_pin(const Pin pin)
{
    LOG_DEBUG();

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
}

void MotorDriver::init_pwm_pin(const Pin pwm_pin)
{
    LOG_DEBUG();

    gpio_set_function(pwm_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pwm_pin);

    pwm_config config = pwm_get_default_config();
    float clkdiv = clock_get_hz(clk_sys) / (PWM_TARGET_HZ * (PWM_WRAP + 1.f));
    if (clkdiv < PWM_CLKDIV_MIN)
        clkdiv = PWM_CLKDIV_MIN;
    else if (clkdiv > PWM_CLKDIV_MAX)
        clkdiv = PWM_CLKDIV_MAX;

    pwm_config_set_clkdiv(&config, clkdiv);
    pwm_config_set_wrap(&config, PWM_WRAP);
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(pwm_pin, 0);
}

void MotorDriver::set_pwm_duty(const Pin pwm_pin, const std::uint16_t duty)
{
    LOG_TRACE();

    assert(is_known_pwm_pin(pwm_pin));

    const std::uint16_t clamped_duty = duty > PWM_FULL_DUTY ? PWM_FULL_DUTY : duty;
    pwm_set_gpio_level(pwm_pin, clamped_duty);
}

bool MotorDriver::is_known_pwm_pin(const Pin pwm_pin) const
{
    return pwm_pin == m_pins.m_motor_a.m_pwm || pwm_pin == m_pins.m_motor_b.m_pwm;
}

bool MotorDriver::is_direction_reversal(const Direction current, const Direction next)
{
    return (current == Direction::Forward && next == Direction::Reverse) ||
           (current == Direction::Reverse && next == Direction::Forward);
}

void MotorDriver::set_motor(const MotorPins& pins, MotorState& motor, const Direction direction, const std::uint16_t speed)
{
    LOG_DEBUG("pins=%u/%u/%u state=%s/%u request=%s/%u",
              static_cast<uint>(pins.m_in1),
              static_cast<uint>(pins.m_in2),
              static_cast<uint>(pins.m_pwm),
              direction_name(motor.m_direction),
              static_cast<uint>(motor.m_speed),
              direction_name(direction),
              static_cast<uint>(speed));

    if (speed > PWM_WRAP)
        LOG_WARNING("speed > PWM_WRAP, defaulting to PWM_WRAP");
    const std::uint16_t clamped_speed = speed > PWM_WRAP ? PWM_WRAP : speed;

    // Drop PWM before changing direction to avoid slamming directly through a reversal.
    if (direction != motor.m_direction)
    {
        set_pwm_duty(pins.m_pwm, 0);
        if (is_direction_reversal(motor.m_direction, direction))
            sleep_us(DIRECTION_CHANGE_DEADTIME_US);
    }

    switch (direction)
    {
    case Direction::Forward:
        gpio_put(pins.m_in1, 1);
        gpio_put(pins.m_in2, 0);
        set_pwm_duty(pins.m_pwm, clamped_speed);
        break;

    case Direction::Reverse:
        gpio_put(pins.m_in1, 0);
        gpio_put(pins.m_in2, 1);
        set_pwm_duty(pins.m_pwm, clamped_speed);
        break;

    case Direction::Brake:
        gpio_put(pins.m_in1, 1);
        gpio_put(pins.m_in2, 1);
        set_pwm_duty(pins.m_pwm, PWM_FULL_DUTY);
        break;

    case Direction::Stop:
    default:
        // TB6612 stop/coast mode is IN1=L, IN2=L, PWM=H with STBY=H.
        gpio_put(pins.m_in1, 0);
        gpio_put(pins.m_in2, 0);
        set_pwm_duty(pins.m_pwm, PWM_FULL_DUTY);
        break;
    }

    motor.m_direction = direction;
    motor.m_speed = clamped_speed;
}
