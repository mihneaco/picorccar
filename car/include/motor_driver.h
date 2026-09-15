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

    constexpr DriverPins MOTOR_DRIVER_PINS{
        {MOTOR_A_IN1, MOTOR_A_IN2, MOTOR_A_PWM},
        {MOTOR_B_IN1, MOTOR_B_IN2, MOTOR_B_PWM},
        MOTOR_DRIVER_STANDBY};

}

class MotorDriver
{
public:
    static constexpr std::uint16_t MAX_PWM_DUTY = 1000;

    /** @brief Bind driver/standby pins. Does not touch hardware; call init() for that. */
    explicit MotorDriver(const pinout::DriverPins p_pins);
    MotorDriver(const MotorDriver &p_other) = delete;
    MotorDriver(MotorDriver &&p_other) = delete;
    MotorDriver &operator=(const MotorDriver &p_otherDriverPins) = delete;
    MotorDriver &operator=(MotorDriver &&p_other) = delete;

    /** @brief Init GPIO/PWM outputs, stop both motors, then assert STBY. */
    void init();
    /** @brief Set signed target duty per motor in [-MAX_PWM_DUTY, MAX_PWM_DUTY]; ramps toward it in service(). */
    void set_target(std::int16_t p_motor_a, std::int16_t p_motor_b);
    /**
     * @brief Advance both motors one slew step toward their targets and drive the hardware.
     * @note Expected to be called once per main loop tick.
     */
    void service();
    /** @brief Assert/deassert STBY. Disabling forces stop_all() first. */
    void set_standby(bool p_enabled);
    /**
     * @brief Immediate stop: zero both target and applied output and drive the outputs to Stop. Bypasses ramp.
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
    /** @brief DriveMode as a string, for logging. */
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
     * @brief Signed motor setpoint: sign is direction, magnitude is PWM duty. Clamped to
     *        [-MAX_PWM_DUTY, MAX_PWM_DUTY] on construction.
     */
    class MotorSetpoint
    {
    public:
        MotorSetpoint() = delete;
        /** @brief Clamp p_value to [-MAX_PWM_DUTY, MAX_PWM_DUTY]; logs a warning if clamped. */
        explicit MotorSetpoint(const std::int16_t p_value);
        std::int16_t value() const { return m_value; }
        DriveMode drive_mode() const
        {
            return m_value > 0   ? DriveMode::Forward
                   : m_value < 0 ? DriveMode::Reverse
                                 : DriveMode::Stop;
        }
        std::uint16_t duty() const { return std::abs(m_value); }

    private:
        std::int16_t m_value{};
    };

    /**
     * @brief Per-motor state: applied output (m_current), prior applied value (m_previous), and
     *        ramp target (m_target).
     */
    class MotorState
    {
    public:
        const MotorSetpoint &current() const { return m_current; }
        const MotorSetpoint &previous() const { return m_previous; }
        const MotorSetpoint &target() const { return m_target; }

        void set_target(const std::int16_t p_value) { m_target = MotorSetpoint{p_value}; }
        /** @brief Shift the applied output, keeping the prior value in m_previous for the drive path's dead-time guard. */
        void set_current(const std::int16_t p_value)
        {
            m_previous = m_current;
            m_current = MotorSetpoint{p_value};
        }
        /** @brief Zero both output and target: the safe state for failsafe / standby. */
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
        const char *const m_name;
        const pinout::MotorPins m_pins;
        MotorState m_state;
    };

    static constexpr std::uint16_t PWM_WRAP = MAX_PWM_DUTY;
    static constexpr std::uint16_t PWM_FULL_DUTY = PWM_WRAP + 1;
    static constexpr std::uint32_t PWM_TARGET_HZ = 20000;
    static constexpr std::uint32_t DIRECTION_CHANGE_DEADTIME_US = 100;
    /**
     * @brief   Max signed-duty change per service() call, split by direction.
     *          This limits peak current draw, brownout risk and reversal thunk
     *          DECEL faster than ACCEL keeps stopping crisp while easing the current inrush on takeoff.
     *          stop_all() bypasses both.
     */
    static constexpr std::int32_t PWM_SLEW_STEP_ACCEL = 100;
    static constexpr std::int32_t PWM_SLEW_STEP_DECEL = 150;

    /** @brief Advance one motor's applied output one slew step toward its target. */
    void service_motor(Motor &p_motor);
    /** @brief Write the current drive mode and PWM duty to hardware, with reversal dead-time. */
    void drive_motor(const Motor &p_motor);

    Motor m_motor_a;
    Motor m_motor_b;
    const pinout::Pin m_standby;
};
