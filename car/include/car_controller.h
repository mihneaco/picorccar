#pragma once

#include <cstdint>

#include "command_receiver.h"
#include "motor_driver.h"
#include "picorccar/protocol.h"

class CarController
{
public:
    struct Config
    {
        std::uint16_t m_adc_min{};
        std::uint16_t m_adc_max{};
        std::uint16_t m_adc_center{};
        std::uint16_t m_adc_deadzone{};
        std::int8_t m_throttle_sign{};
        std::int8_t m_steer_sign{};
        std::int8_t m_motor_a_sign{};
        std::int8_t m_motor_b_sign{};
        std::uint16_t m_max_pwm_duty{};
    };

    static constexpr Config ConfigDefault = {
        .m_adc_min      = 0,
        .m_adc_max      = 4095,
        .m_adc_center   = 2048,
        .m_adc_deadzone = 128,
        .m_throttle_sign = -1,
        .m_steer_sign    = 1,
        .m_motor_a_sign  = -1,
        .m_motor_b_sign  = 1,
        .m_max_pwm_duty  = MotorDriver::MAX_PWM_DUTY,
    };

    // Same as ConfigDefault but caps commanded duty at half the driver max for controllability
    // and to limit peak current draw / brownout risk.
    static constexpr Config ConfigHalfDuty = {
        .m_adc_min       = 0,
        .m_adc_max       = 4095,
        .m_adc_center    = 2048,
        .m_adc_deadzone  = 128,
        .m_throttle_sign = -1,
        .m_steer_sign    = 1,
        .m_motor_a_sign  = -1,
        .m_motor_b_sign  = 1,
        .m_max_pwm_duty  = MotorDriver::MAX_PWM_DUTY / 2,
    };

    CarController(CommandReceiver& p_command_receiver,
                  MotorDriver& p_motor_driver,
                  Config p_config);

    bool init();
    void run();

private:
    struct MotorCommand
    {
        MotorDriver::DriveMode m_drive_mode = MotorDriver::DriveMode::Stop;
        std::uint16_t m_pwm_duty = 0;
    };

    void apply(const protocol::CtrlState& p_ctrl_state);
    void stop();
    std::int32_t axis_to_signed_command(std::uint16_t p_adc_value, std::int8_t p_sign) const;
    MotorCommand to_motor_command(std::int32_t p_signed_command) const;

    CommandReceiver& m_command_receiver;
    MotorDriver& m_motor_driver;
    Config m_config;
    bool m_initialized = false;
};
