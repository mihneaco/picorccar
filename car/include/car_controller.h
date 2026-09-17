#pragma once

#include <cstdint>

#include "command_receiver.h"
#include "motor_driver.h"
#include "picorccar/protocol.h"

namespace Internal
{
/**
 * @brief Tuning for the ADC mapping, axis/motor polarity, and duty limits.
 * @note compiler complains because of the defaults if decalred inside CarController
 *       using a using statement there
 */
struct CarConfig
{
    std::uint16_t m_adc_min{0};
    std::uint16_t m_adc_max{4095};
    std::uint16_t m_adc_center{2048};
    std::uint16_t m_adc_deadzone{128};
    std::int8_t m_throttle_sign{-1};
    std::int8_t m_steer_sign{-1};
    std::int8_t m_motor_a_sign{-1};
    std::int8_t m_motor_b_sign{1};
    std::uint16_t m_max_pwm_duty{MotorDriver::MAX_PWM_DUTY};
    /**
     * @brief Steer contribution to the differential mix, as a percentage of the computed
     *        steer command in [0, 100). See set_target() for why this can't be 100.
     */
    std::uint8_t m_steer_scale_percent{50};
};
} // namespace Internal

class CarController
{
public:
    using Config = Internal::CarConfig;

    static constexpr Config ConfigMaxDuty = {};
    static constexpr Config ConfigReducedDuty = {.m_max_pwm_duty =
                                                     MotorDriver::MAX_PWM_DUTY * 3 / 4};
    static constexpr Config ACTIVE_CONFIG = ConfigMaxDuty;

    /** @brief Bind the receiver, driver, and config to run against. */
    CarController(CommandReceiver& p_command_receiver,
                  MotorDriver& p_motor_driver,
                  Config p_config);

    /** @brief Init the receiver and driver. */
    bool init();
    /** @brief Main loop: poll commands, drive motors, and failsafe on timeout. */
    void run();

private:
    /**
     * @brief Translate a control packet into signed per-motor targets and hand them
     *        to the driver, which ramps toward those targets.
     */
    void set_target(const protocol::CtrlState& p_ctrl_state);
    /** @brief Immediate failsafe stop. Delegates to the driver, which bypasses its ramp. */
    void stop();

    /**
     * @brief Map a raw ADC axis value to a signed duty command in [-m_max_pwm_duty,
     *        m_max_pwm_duty].
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
