#include "remote_controller.h"

#include "picorccar/logger.h"
#include "picorccar/protocol.h"

#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;
}

RemoteController::RemoteController(JoystickController& p_joystick_controller,
                                   CommandSender& p_command_sender)
    : m_joystick_controller(p_joystick_controller),
      m_command_sender(p_command_sender)
{
}

bool RemoteController::init()
{
    if (m_initialized)
    {
        LOG_WARNING("Remote controller already initialized");
        return true;
    }

    LOG_INFO("Initializing Joystick Controller");
    if (!m_joystick_controller.init())
    {
        LOG_CRITICAL("Joystick controller initialization failed");
        return false;
    }

    LOG_INFO("Initializing Command Sender");
    if (!m_command_sender.init())
    {
        LOG_CRITICAL("Command sender initialization failed");
        return false;
    }

    m_initialized = true;
    return true;
}

void RemoteController::run()
{
    if (!m_initialized)
    {
        LOG_WARNING("Remote controller run requested before initialization");
        return;
    }

    LOG_INFO("Starting Controller MAIN LOOP");
    JoystickController::Sample joystick_sample{};
    while (true)
    {
        if (m_joystick_controller.read(joystick_sample))
            handle_joystick_sample(joystick_sample);

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }
}

void RemoteController::handle_joystick_sample(const JoystickController::Sample& p_sample)
{
    handle_joystick_button(p_sample.m_bpressed);
    handle_joystick_position(p_sample);
}

void RemoteController::handle_joystick_button(const bool p_button_pressed)
{
    if (p_button_pressed == m_was_button_pressed)
        return;

    LOG_INFO("Joystick button %s", p_button_pressed ? "pressed" : "released");
    m_was_button_pressed = p_button_pressed;

    if (!p_button_pressed)
        return;

    m_session_started = m_command_sender.start_new_session();
    m_last_sent_controller_state.reset();
    m_last_successful_send_ms = 0;
    LOG_INFO("Button-triggered session restart %s",
             m_session_started ? "succeeded" : "failed");
}

void RemoteController::handle_joystick_position(const JoystickController::Sample& p_sample)
{
    if (!m_session_started)
        return;

    const protocol::CtrlState controller_state{
        p_sample.m_x_axis,
        p_sample.m_y_axis};
    const std::uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    const bool keep_alive_due =
        m_last_sent_controller_state.has_value()
        && (now_ms - m_last_successful_send_ms) >= protocol::KEEP_ALIVE_MS;

    if (!m_last_sent_controller_state.has_value()
        || !controller_state.is_approx_eq(*m_last_sent_controller_state)
        || keep_alive_due)
    {
        if (m_command_sender.send_controller_state(controller_state))
        {
            m_last_sent_controller_state = controller_state;
            m_last_successful_send_ms = now_ms;
        }
    }
}
