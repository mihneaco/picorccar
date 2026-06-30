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

    /**
     * @brief Per-motor signed duty: the sign picks direction, the magnitude is PWM duty.
     * @details Clamped to [-MAX_PWM_DUTY, MAX_PWM_DUTY] on construction, so holding one
     *          guarantees a value the hardware can apply. A default-constructed command is
     *          0 (Stop), the safe default for failsafe/standby state.
     */
    class MotorCommand
    {
    public:
        MotorCommand() = default;
        explicit MotorCommand(std::int32_t p_value);
        std::int32_t value() const { return m_value; }
        DriveMode drive_mode() const;
        std::uint16_t duty() const;

    private:
        std::int32_t m_value = 0;
    };

    /// @brief One MotorCommand per motor: the command handed to the driver via set_target().
    struct DriverCommand
    {
        MotorCommand m_motor_a;
        MotorCommand m_motor_b;
    };

    explicit MotorDriver(const pinout::DriverPins p_pins);
    MotorDriver(const MotorDriver& p_other) = delete;
    MotorDriver(MotorDriver&& p_other) = delete;
    MotorDriver& operator=(const MotorDriver &p_otherDriverPins) = delete;
    MotorDriver& operator=(MotorDriver&& p_other) = delete;

    void init();
    /**
     * @brief Set the signed target duty per motor in [-MAX_PWM_DUTY, MAX_PWM_DUTY]:
     *        the sign picks direction, the magnitude is PWM duty.
     * @details The applied output is not changed here; it ramps toward these targets
     *          one step per service() call.
     */
    void set_target(DriverCommand p_command);
    /**
     * @brief Advance both motors one slew step toward their targets and drive the hardware.
     * @note Expected to be called once per control tick.
     */
    void service();
    void set_standby(bool p_enabled);
    /**
     * @brief Immediate stop: zero both target and applied output and drive the outputs to Stop.
     * @note Bypasses the ramp; intended for failsafe / standby.
     */
    void stop_all();

private:
    /**
     * @brief Target and currently-applied command for one motor.
     * @details Sign encodes direction, so a reversal is just the value crossing zero
     *          and the ramp handles it without extra state.
     */
    struct MotorState
    {
        MotorCommand m_target;
        MotorCommand m_current;

        /**
         * @brief Advance m_current one slew step toward m_target, capped at p_max_step.
         * @details A sign change lands on a Stop step (m_current = 0) instead of slamming
         *          across, so direction never flips under load.
         */
        void slew_toward_target();
    };

    static constexpr std::uint16_t PWM_WRAP = MAX_PWM_DUTY;
    static constexpr std::uint16_t PWM_FULL_DUTY = PWM_WRAP + 1;
    static constexpr std::uint32_t PWM_TARGET_HZ = 20000;
    static constexpr std::uint32_t DIRECTION_CHANGE_DEADTIME_US = 100;
    /**
     * @brief   Max signed-duty change per service() call.
     * @details With a ~20 ms control tick this ramps the full 0..MAX_PWM_DUTY range in
     *          ~MAX_PWM_DUTY/PWM_SLEW_STEP calls (1000/100 = 10 ~= 200 ms) and a full
     *          forward<->reverse swing in ~2x that. Capping di/dt this way limits peak
     *          current draw, brownout risk, and the audible reversal thunk; thermals are
     *          unchanged at steady state and the trade is slightly softer throttle
     *          response. stop_all() bypasses it.
     */
    static constexpr std::int32_t PWM_SLEW_STEP = 100;

    static bool is_drive_mode_reversal(DriveMode p_current, DriveMode p_next);

    void service_motor(const pinout::MotorPins& p_pins, MotorState& p_motor);
    void drive_motor(const pinout::MotorPins& p_pins,
                     DriveMode p_previous_mode,
                     DriveMode p_drive_mode,
                     std::uint16_t p_pwm_duty);

    const pinout::DriverPins m_pins;
    MotorState m_motor_state_a;
    MotorState m_motor_state_b;
};
