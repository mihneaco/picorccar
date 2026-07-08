#include "car_controller.h"

#include "picorccar/logger.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;
/// Range-collapse instrumentation: sample the controller's RSSI at a rate the log can absorb.
constexpr std::uint32_t RSSI_LOG_PERIOD_MS = 1000;
#ifdef PICORCCAR_DEBUG
constexpr std::uint32_t DEBUG_PACKET_TRACE_PERIOD_MS = 500;
#endif

void print_packet(const CommandReceiver::ReceivedCommand& p_received_command)
{
#ifdef PICORCCAR_DEBUG
    static std::uint32_t last_packet_trace_ms = 0;
    if ((p_received_command.m_received_ms - last_packet_trace_ms) < DEBUG_PACKET_TRACE_PERIOD_MS)
        return;

    last_packet_trace_ms = p_received_command.m_received_ms;
    LOG_TRACE("UDP packet x=%u y=%u sent_ms=%u received_ms=%u",
              static_cast<unsigned>(p_received_command.m_ctrl_state.m_x_axis),
              static_cast<unsigned>(p_received_command.m_ctrl_state.m_y_axis),
              static_cast<unsigned>(p_received_command.m_sent_ms),
              static_cast<unsigned>(p_received_command.m_received_ms));
#else
    (void)p_received_command;
#endif
}
}

CarController::CarController(CommandReceiver& p_command_receiver,
                             MotorDriver& p_motor_driver,
                             const Config p_config)
    : m_command_receiver(p_command_receiver),
      m_motor_driver(p_motor_driver),
      m_config(p_config)
{
    if (m_config.m_max_pwm_duty > MotorDriver::MAX_PWM_DUTY)
        m_config.m_max_pwm_duty = MotorDriver::MAX_PWM_DUTY;
}

bool CarController::init()
{
    if (m_initialized)
    {
        LOG_WARNING("Car controller already initialized");
        return true;
    }

    LOG_INFO("Initializing Motor Driver");
    m_motor_driver.init();

    LOG_INFO("Initializing Command Receiver");
    if (!m_command_receiver.init())
    {
        LOG_CRITICAL("Command receiver initialization failed");
        m_motor_driver.set_standby(false);
        return false;
    }

    m_initialized = true;
    return true;
}

void CarController::run()
{
    if (!m_initialized)
    {
        LOG_WARNING("Car controller run requested before initialization");
        return;
    }

    LOG_INFO("Starting MAIN loop");
    CommandReceiver::ReceivedCommand latest_received_command{};
    std::uint32_t last_packet_ms{to_ms_since_boot(get_absolute_time())};
    std::uint32_t last_rssi_log_ms{};
    bool motors_stopped{false};
    while (true)
    {
        const bool has_packet = m_command_receiver.get_packet(latest_received_command);
        if (has_packet)
        {
            last_packet_ms = latest_received_command.m_received_ms;
            motors_stopped = false;
            set_target(latest_received_command.m_ctrl_state);
            print_packet(latest_received_command);
        }

        if (m_command_receiver.consume_restart_request())
        {
            // Failsafe stop before the restart: the Wi-Fi bounce blocks this loop for a
            // while, so the motors must not be left driving through it.
            stop();
            motors_stopped = true;
            LOG_WARNING("Remote Wi-Fi restart requested");
            if (!m_command_receiver.restart_wifi())
                LOG_CRITICAL("Wi-Fi restart failed; motors stopped, no command source");
            last_packet_ms = to_ms_since_boot(get_absolute_time());
        }

        const std::uint32_t packet_age_ms = to_ms_since_boot(get_absolute_time()) - last_packet_ms;
        if (!motors_stopped && packet_age_ms > protocol::ACTIVE_TIMING.m_command_timeout_ms)
        {
            stop();
            LOG_INFO("Command timeout: motors stopped");
            motors_stopped = true;
        }

        // Advance the duty ramp every tick, independent of packet arrival, so the applied
        // duty keeps chasing the latest target between commands.
        m_motor_driver.service();

        /*
         * Poll RSSI only while the motors are already stopped: the query issues blocking
         * CYW43 ioctls that can stall this loop for the full driver timeout, which must
         * never delay the command-timeout failsafe while the motors are driving. The
         * degraded-link state we are instrumenting stops the motors via that failsafe
         * anyway, so the interesting samples are still captured.
         */
        const std::uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (motors_stopped && now_ms - last_rssi_log_ms >= RSSI_LOG_PERIOD_MS)
        {
            last_rssi_log_ms = now_ms;
            if (const std::optional<std::int32_t> rssi = m_command_receiver.read_client_rssi())
                LOG_INFO("Client RSSI %ld dBm", static_cast<long>(*rssi));
        }

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }
}

void CarController::set_target(const protocol::CtrlState& p_ctrl_state)
{
    const std::int32_t throttle_command = axis_to_signed_command(p_ctrl_state.m_y_axis,
                                                                 m_config.m_throttle_sign);
    const std::int32_t steer_command = axis_to_signed_command(p_ctrl_state.m_x_axis,
                                                              m_config.m_steer_sign);
    const std::int32_t max_pwm_duty = static_cast<std::int32_t>(m_config.m_max_pwm_duty);

    // Differential-drive mix: throttle drives both motors together, steering biases them apart.
    // Standard convention: positive steer = counter-clockwise (left turn), so the left motor (A)
    // slows and the right motor (B) speeds up.
    const std::int32_t motor_a_command =
        std::clamp((throttle_command - steer_command) * m_config.m_motor_a_sign,
                   -max_pwm_duty,
                   max_pwm_duty);
    const std::int32_t motor_b_command =
        std::clamp((throttle_command + steer_command) * m_config.m_motor_b_sign,
                   -max_pwm_duty,
                   max_pwm_duty);

    m_motor_driver.set_target(motor_a_command, motor_b_command);
}

void CarController::stop()
{
    m_motor_driver.stop_all();
}

std::int32_t CarController::axis_to_signed_command(const std::uint16_t p_adc_value,
                                                   const std::int8_t p_sign) const
{
    const std::int32_t signed_delta = static_cast<std::int32_t>(p_adc_value) -
                                      static_cast<std::int32_t>(m_config.m_adc_center);
    const std::int32_t abs_delta = std::abs(signed_delta);
    if (abs_delta <= static_cast<std::int32_t>(m_config.m_adc_deadzone))
        return 0;

    const bool is_positive = signed_delta > 0;
    const std::int32_t axis_limit = is_positive
        ? static_cast<std::int32_t>(m_config.m_adc_max) - static_cast<std::int32_t>(m_config.m_adc_center)
        : static_cast<std::int32_t>(m_config.m_adc_center) - static_cast<std::int32_t>(m_config.m_adc_min);
    const std::int32_t usable_range = axis_limit - static_cast<std::int32_t>(m_config.m_adc_deadzone);
    if (usable_range <= 0)
        return 0;

    const std::int32_t adjusted_delta = abs_delta - static_cast<std::int32_t>(m_config.m_adc_deadzone);
    const std::int32_t scaled_command =
        (adjusted_delta * static_cast<std::int32_t>(m_config.m_max_pwm_duty)) / usable_range;
    const std::int32_t signed_command = is_positive ? scaled_command : -scaled_command;

    return std::clamp(signed_command * p_sign,
                      -static_cast<std::int32_t>(m_config.m_max_pwm_duty),
                      static_cast<std::int32_t>(m_config.m_max_pwm_duty));
}
