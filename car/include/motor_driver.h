#pragma once

#include <cstdint>

#include "pinout.h"

namespace pinout
{
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

constexpr DriverPins MOTOR_DRIVER_PINS
{
    {MOTOR_A_IN1, MOTOR_A_IN2, MOTOR_A_PWM},
    {MOTOR_B_IN1, MOTOR_B_IN2, MOTOR_B_PWM},
    MOTOR_DRIVER_STANDBY
};

}


class MotorDriver
{
public:
    static constexpr std::uint16_t MAX_PWM_DUTY = 1000;

    enum class DriveMode
    {
        Stop,
        Forward,
        Reverse,
        Brake
    };

    explicit MotorDriver(const pinout::DriverPins p_pins);
    MotorDriver(const MotorDriver& p_other) = delete;
    MotorDriver(MotorDriver&& p_other) = delete;
    MotorDriver& operator=(const MotorDriver &p_otherDriverPins) = delete;
    MotorDriver& operator=(MotorDriver&& p_other) = delete;

    void init();
    void set_motor_a(const DriveMode p_drive_mode, const std::uint16_t p_speed);
    void set_motor_b(const DriveMode p_drive_mode, const std::uint16_t p_speed);
    void set_standby(bool p_enabled);
    void stop_all();

private:
    struct MotorState
    {
        DriveMode m_drive_mode = DriveMode::Stop;
        std::uint16_t m_pwm_duty = 0;
    };

    static constexpr std::uint16_t PWM_WRAP = MAX_PWM_DUTY;
    static constexpr std::uint16_t PWM_FULL_DUTY = PWM_WRAP + 1;
    static constexpr std::uint32_t PWM_TARGET_HZ = 20000;
    static constexpr std::uint32_t DIRECTION_CHANGE_DEADTIME_US = 100;

    static bool is_drive_mode_reversal(const DriveMode p_current, const DriveMode p_next);

    void set_motor(const pinout::MotorPins& p_pins,
                   MotorState& p_motor,
                   const DriveMode p_drive_mode,
                   const std::uint16_t p_speed);

    const pinout::DriverPins m_pins;
    MotorState m_motor_state_a;
    MotorState m_motor_state_b;
};
