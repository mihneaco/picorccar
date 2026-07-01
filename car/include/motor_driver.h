#pragma once

#include <cstdint>
#include <cstdlib>

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
    void set_target(std::int16_t p_motor_a, std::int16_t p_motor_b);
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
    // Hardware output vocabulary for a single motor, shared by the drive path.
    enum class DriveMode
    {
        Stop,
        Forward,
        Reverse
    };
    static constexpr const char *drive_mode_name(const DriveMode p_drive_mode)
    {
        switch (p_drive_mode)
        {
        case DriveMode::Stop:
            return "Stop";
        case DriveMode::Forward:
            return "Forward";
        case DriveMode::Reverse:
            return "Reverse";
        default:
            return "Unknown";
        }
    }

    /**
     * @brief Signed motor setpoint: the sign picks direction, the magnitude is PWM duty.
     * @details Clamped to [-MAX_PWM_DUTY, MAX_PWM_DUTY] on construction, so holding one
     *          guarantees a value the hardware can apply. A zero value (MotorSetpoint{0}) is
     *          Stop, the safe default for failsafe/standby state.
     * @warning An out-of-range input is clamped rather than rejected, and construction emits
     *          a LOG_WARNING when it happens -- that log should be treated as a caller bug.
     *          Because it can log, do not construct a MotorSetpoint from an IRQ/timer path.
     */
    class MotorSetpoint
    {
    public:
        MotorSetpoint() = delete;
        explicit MotorSetpoint(const std::int16_t p_value);
        std::int16_t value() const { return m_value; }
        DriveMode drive_mode() const
        {
            return  m_value > 0 ? DriveMode::Forward
                  : m_value < 0 ? DriveMode::Reverse
                  : DriveMode::Stop;
        }
        std::uint16_t duty() const { return std::abs(m_value); }

    private:
        std::int16_t m_value{};
    };

    /**
     * @brief Per-motor state: the applied output (m_current), the value it was driven at
     *        before this tick (m_previous), and where it is ramping to (m_target).
     * @details service() advances m_current one slew step toward m_target each tick.
     *          set_current() shifts the prior applied value into m_previous so the drive path
     *          can see the mode it is transitioning away from without a separate argument.
     *          Fields are private so m_current can never be moved without shifting m_previous.
     */
    class MotorState
    {
    public:
        const MotorSetpoint& current() const { return m_current; }
        const MotorSetpoint& previous() const { return m_previous; }
        const MotorSetpoint& target() const { return m_target; }

        void set_target(const std::int16_t p_value) { m_target = MotorSetpoint{p_value}; }
        // Shift the applied output, keeping the prior value in m_previous for the drive
        // path's dead-time guard.
        void set_current(const std::int16_t p_value)
        {
            m_previous = m_current;
            m_current = MotorSetpoint{p_value};
        }
        // Zero both output and target: the safe state for failsafe / standby.
        void reset()
        {
            m_previous = m_current;
            m_current = MotorSetpoint{0};
            m_target = MotorSetpoint{0};
        }

    private:
        MotorSetpoint m_current{0};
        MotorSetpoint m_previous{0};
        MotorSetpoint m_target{0};
    };

    struct Motor
    {
        const char* const m_name;
        const pinout::MotorPins m_pins;
        MotorState m_state;
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

    void service_motor(Motor& p_motor);
    void drive_motor(const Motor& p_motor);

    Motor m_motor_a;
    Motor m_motor_b;
    const pinout::Pin m_standby;
};
