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
        static constexpr std::uint16_t ADC_MIN = 0;
        static constexpr std::uint16_t ADC_MAX = 4095;
        static constexpr std::uint16_t ADC_CENTER = 2048;
        static constexpr std::uint16_t ADC_DEADZONE = 128;
        static constexpr std::int8_t THROTTLE_SIGN = 1;
        static constexpr std::int8_t STEER_SIGN = 1;
        static constexpr std::int8_t MOTOR_A_SIGN = 1;
        static constexpr std::int8_t MOTOR_B_SIGN = 1;

        std::uint16_t m_adc_min = ADC_MIN;
        std::uint16_t m_adc_max = ADC_MAX;
        std::uint16_t m_adc_center = ADC_CENTER;
        std::uint16_t m_adc_deadzone = ADC_DEADZONE;
        std::int8_t m_throttle_sign = THROTTLE_SIGN;
        std::int8_t m_steer_sign = STEER_SIGN;
        std::int8_t m_motor_a_sign = MOTOR_A_SIGN;
        std::int8_t m_motor_b_sign = MOTOR_B_SIGN;
        std::uint16_t m_max_pwm_duty = MotorDriver::MAX_PWM_DUTY;
    };

    CarController(CommandReceiver& p_command_receiver,
                  MotorDriver& p_motor_driver);
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
