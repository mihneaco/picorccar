#include "remote_controller.h"

#include "picorccar/logger.h"
#include "picorccar/protocol.h"

#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;

// The session toggle must be a deliberate gesture: the button has to be held
// for SESSION_TOGGLE_HOLD_MS while the stick stays near center. This rejects
// the accidental Z-button actuation that happens when the stick is shoved into
// a corner.
constexpr std::uint32_t SESSION_TOGGLE_HOLD_MS = 500;
constexpr std::uint16_t SESSION_TOGGLE_CENTER_TOLERANCE = 512;
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
    while (true)
    {
        if (!m_command_sender.is_connected() && !m_command_sender.connect())
            continue;

        if (const std::optional<JoystickController::Sample> joystick_sample = m_joystick_controller.read())
            handle_joystick_sample(*joystick_sample);

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }
}

void RemoteController::handle_joystick_sample(const JoystickController::Sample& p_sample)
{
    handle_joystick_button(p_sample);
    handle_joystick_position(p_sample);
}

void RemoteController::handle_joystick_button(const JoystickController::Sample& p_sample)
{
    if (!p_sample.m_bpressed)
    {
        // Released: re-arm the gesture for the next press.
        m_button_hold_start_ms.reset();
        m_fired = false;
        return;
    }

    // Any drift away from center cancels the in-progress hold so a corner press
    // never accumulates toward the toggle.
    if (p_sample.max_center_offset() > SESSION_TOGGLE_CENTER_TOLERANCE)
    {
        m_button_hold_start_ms.reset();
        m_fired = false;
        return;
    }

    if (m_fired == true)
        return;

    const std::uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (!m_button_hold_start_ms.has_value())
        m_button_hold_start_ms = now_ms;
    if (now_ms - *m_button_hold_start_ms > SESSION_TOGGLE_HOLD_MS)
    {
        if (m_session_started)
        {
            // Relinquish control by going silent; the car's command timeout failsafe
            // then stops and de-arms it.
            m_session_started = false;
            LOG_INFO("Session relinquished; car will failsafe-stop");
        }
        else
        {
            m_session_started = m_command_sender.start_new_session();
            LOG_INFO("Session start %s", m_session_started ? "succeeded" : "failed");
        }

        m_fired = true;
    }
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
        && (now_ms - m_last_successful_send_ms) >= protocol::ACTIVE_TIMING.m_command_interval_ms;

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
