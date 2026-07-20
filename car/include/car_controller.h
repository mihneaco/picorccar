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
        /**
         * @brief Steer contribution to the differential mix, as a percentage of the computed
         *        steer command in [0, 100]. See set_target() for why this can't be 100.
         */
        std::uint8_t m_steer_scale_percent{};
    };

    static constexpr Config ConfigMaxDuty = {
        .m_adc_min       = 0,
        .m_adc_max       = 4095,
        .m_adc_center    = 2048,
        .m_adc_deadzone  = 128,
        .m_throttle_sign = -1,
        .m_steer_sign    = -1,
        .m_motor_a_sign  = -1,
        .m_motor_b_sign  = 1,
        .m_max_pwm_duty  = MotorDriver::MAX_PWM_DUTY,
        .m_steer_scale_percent = 50
    };
    /**
     * @brief Same as ConfigDefault but caps commanded duty for controllability and to limit peak current draw / brownout risk.
     */
    static constexpr Config ConfigReducedDuty = {
        .m_adc_min       = 0,
        .m_adc_max       = 4095,
        .m_adc_center    = 2048,
        .m_adc_deadzone  = 128,
        .m_throttle_sign = -1,
        .m_steer_sign    = -1,
        .m_motor_a_sign  = -1,
        .m_motor_b_sign  = 1,
        .m_max_pwm_duty  = MotorDriver::MAX_PWM_DUTY * 3/4,
        .m_steer_scale_percent = 50
    };
    static constexpr Config ACTIVE_CONFIG = ConfigMaxDuty;

    CarController(CommandReceiver & p_command_receiver,
                  MotorDriver &p_motor_driver,
                  Config p_config);

    bool init();
    void run();

private:
    /**
     * @brief Translate a control packet into signed per-motor targets and hand them
     *        to the driver, which owns the ramp toward those targets.
     * @details Mixes throttle and steer as motor_a = throttle - steer, motor_b = throttle + steer.
     *          Steer is scaled by m_steer_scale_percent before mixing: at 100%, any diagonal stick
     *          position with |throttle| == |steer| cancels motor_a to exactly zero regardless of
     *          how far the stick is pushed, which reads as a snap pivot instead of a graduated
     *          turn. Scaling steer down moves that cancellation point outside the normal steering
     *          range.
     */
    void set_target(const protocol::CtrlState& p_ctrl_state);
    /// @brief Immediate failsafe stop. Delegates to the driver, which bypasses its ramp.
    void stop();
    /**
     * @brief Map a raw ADC axis value to a signed duty command in [-m_max_pwm_duty, m_max_pwm_duty].
     * @details Applies the center deadzone, then a blended linear/quadratic expo curve so small
     *          stick deflections command proportionally less duty for finer low-speed control,
     *          while keeping the top-end response closer to linear than a pure square curve would
     *          -- full deflection still reaches m_max_pwm_duty exactly either way.
     */
    std::int32_t axis_to_signed_command(std::uint16_t p_adc_value, std::int8_t p_sign) const;

    CommandReceiver& m_command_receiver;
    MotorDriver& m_motor_driver;
    Config m_config;
    bool m_initialized = false;
};
