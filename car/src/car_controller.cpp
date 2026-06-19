#include "car_controller.h"

#include "picorccar/logger.h"

#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;
#ifdef PICORCCAR_DEBUG
constexpr std::uint32_t DEBUG_PACKET_TRACE_PERIOD_MS = 500;
#endif

constexpr std::int32_t clamp_signed(const std::int32_t p_value,
                                    const std::int32_t p_min,
                                    const std::int32_t p_max)
{
    if (p_value < p_min)
        return p_min;

    if (p_value > p_max)
        return p_max;

    return p_value;
}

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
                             MotorDriver& p_motor_driver)
    : CarController(p_command_receiver, p_motor_driver, Config{})
{
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
    m_motor_driver.stop_all();

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
    bool command_timed_out{false};
    while (true)
    {
        const bool has_packet = m_command_receiver.get_packet(latest_received_command);
        if (has_packet)
        {
            last_packet_ms = latest_received_command.m_received_ms;
            command_timed_out = false;
            apply(latest_received_command.m_ctrl_state);
            print_packet(latest_received_command);
        }

        const std::uint32_t packet_age_ms = to_ms_since_boot(get_absolute_time()) - last_packet_ms;
        if (!command_timed_out && packet_age_ms > (2u * protocol::KEEP_ALIVE_MS))
        {
            stop();
            m_command_receiver.reset_session();
            command_timed_out = true;
        }

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }
}

void CarController::apply(const protocol::CtrlState& p_ctrl_state)
{
    const std::int32_t throttle_command = axis_to_signed_command(p_ctrl_state.m_y_axis,
                                                                 m_config.m_throttle_sign);
    const std::int32_t steer_command = axis_to_signed_command(p_ctrl_state.m_x_axis,
                                                              m_config.m_steer_sign);
    const std::int32_t max_pwm_duty = static_cast<std::int32_t>(m_config.m_max_pwm_duty);

    // Differential-drive mix: throttle drives both motors together, steering biases them apart.
    const std::int32_t motor_a_command =
        clamp_signed((throttle_command + steer_command) * m_config.m_motor_a_sign,
                     -max_pwm_duty,
                     max_pwm_duty);
    const std::int32_t motor_b_command =
        clamp_signed((throttle_command - steer_command) * m_config.m_motor_b_sign,
                     -max_pwm_duty,
                     max_pwm_duty);

    const MotorCommand motor_a = to_motor_command(motor_a_command);
    const MotorCommand motor_b = to_motor_command(motor_b_command);

    m_motor_driver.set_motor_a(motor_a.m_drive_mode, motor_a.m_pwm_duty);
    m_motor_driver.set_motor_b(motor_b.m_drive_mode, motor_b.m_pwm_duty);
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
    const std::int32_t abs_delta = signed_delta >= 0 ? signed_delta : -signed_delta;
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

    return clamp_signed(signed_command * p_sign,
                        -static_cast<std::int32_t>(m_config.m_max_pwm_duty),
                        static_cast<std::int32_t>(m_config.m_max_pwm_duty));
}

CarController::MotorCommand CarController::to_motor_command(const std::int32_t p_signed_command) const
{
    MotorCommand motor_command{};

    if (p_signed_command > 0)
    {
        motor_command.m_drive_mode = MotorDriver::DriveMode::Forward;
        motor_command.m_pwm_duty = static_cast<std::uint16_t>(p_signed_command);
    }
    else if (p_signed_command < 0)
    {
        motor_command.m_drive_mode = MotorDriver::DriveMode::Reverse;
        motor_command.m_pwm_duty = static_cast<std::uint16_t>(-p_signed_command);
    }

    return motor_command;
}
