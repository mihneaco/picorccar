#pragma once

#include <cstdint>

#include "pico/types.h"

class MotorDriver
{
public:
    //  Pico SDK uses uint for most of its apis that take a GPIO pin number so use that
    using Pin = uint;

    struct MotorPins
    {
        Pin m_in1;
        Pin m_in2;
        Pin m_pwm;
    };

    struct DriverPins
    {
        MotorPins m_motor_a;
        MotorPins m_motor_b;
        Pin m_standby;
    };

    enum class Direction
    {
        Stop,
        Forward,
        Reverse,
        Brake
    };

    static constexpr std::uint16_t MAX_SPEED = 1000;

    explicit MotorDriver(const DriverPins p_pins);

    void init();
    void set_motor_a(const Direction p_direction, const std::uint16_t p_speed);
    void set_motor_b(const Direction p_direction, const std::uint16_t p_speed);
    void stop_all();

private:
    struct MotorState
    {
        Direction m_direction = Direction::Stop;
        std::uint16_t m_speed = 0;
    };

    static constexpr std::uint16_t PWM_WRAP = MAX_SPEED;
    static constexpr std::uint16_t PWM_FULL_DUTY = PWM_WRAP + 1;
    static constexpr std::uint32_t PWM_TARGET_HZ = 20000;
    static constexpr float PWM_CLKDIV_MIN = 1.0f;
    // Pico PWM clock divider is 8.4 fixed-point, so the largest value is 255 + 15/16.
    static constexpr float PWM_CLKDIV_MAX = 255.0f + (15.0f / 16.0f);
    static constexpr std::uint32_t DIRECTION_CHANGE_DEADTIME_US = 100;

    void init_pins();
    void init_control_pin(const Pin p_pin);
    void init_pwm_pin(const Pin p_pwm_pin);
    void set_pwm_duty(const Pin p_pwm_pin, const std::uint16_t p_duty);
    bool is_known_pwm_pin(const Pin p_pwm_pin) const;
    static bool is_direction_reversal(const Direction p_current, const Direction p_next);

    void set_motor(const MotorPins& p_pins,
                   MotorState& p_motor,
                   const Direction p_direction,
                   const std::uint16_t p_speed);

    const DriverPins m_pins;
    MotorState m_motor_state_a;
    MotorState m_motor_state_b;
};
