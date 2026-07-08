#include "remote_controller.h"

#include "picorccar/logger.h"
#include "picorccar/protocol.h"

#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;

/**
 * @brief The Wi-Fi restart must be a deliberate gesture: the button has to be held
 *        for WIFI_RESTART_HOLD_MS while the stick stays near center. This rejects
 *        the accidental Z-button actuation that happens when the stick is shoved into
 *        a corner.
 */
constexpr std::uint32_t WIFI_RESTART_HOLD_MS = 500;
constexpr std::uint16_t WIFI_RESTART_CENTER_TOLERANCE = 512;

/**
 * @brief Delay between commanding the car's Wi-Fi restart and bouncing our own stack,
 *        so the RST packets drain out of the driver before it is torn down.
 */
constexpr std::uint32_t LOCAL_WIFI_RESTART_DELAY_MS = 500;

/// Range-collapse instrumentation: sample the AP link RSSI at a rate the log can absorb.
constexpr std::uint32_t RSSI_LOG_PERIOD_MS = 1000;

/**
 * @brief Join watchdog deadline: a healthy join completes in ~3 s, so a link that has been
 *        down this long is assumed stuck in the driver's internal rejoin loop and the whole
 *        stack is restarted. Generous enough to never preempt a genuine slow join.
 */
constexpr std::uint32_t WIFI_JOIN_WATCHDOG_MS = 15000;
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
    m_last_link_up_ms = to_ms_since_boot(get_absolute_time());
    while (true)
    {
        const bool connected = m_command_sender.is_connected() || m_command_sender.connect();
        const std::uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (connected)
        {
            m_last_link_up_ms = now_ms;

            // Arm as soon as the link is up, and re-arm with a fresh session after every
            // link loss or Wi-Fi restart.
            if (!m_session_started)
            {
                m_session_started = m_command_sender.start_new_session();
                LOG_INFO("Session start %s", m_session_started ? "succeeded" : "failed");
            }

            if (const std::optional<JoystickController::Sample> joystick_sample = m_joystick_controller.read())
                handle_joystick_sample(*joystick_sample);

            if (now_ms - m_last_rssi_log_ms >= RSSI_LOG_PERIOD_MS)
            {
                m_last_rssi_log_ms = now_ms;
                if (const std::optional<std::int32_t> rssi = m_command_sender.read_rssi())
                    LOG_INFO("RSSI %ld dBm", static_cast<long>(*rssi));
            }
        }
        else
        {
            m_session_started = false;

            // Join watchdog: escape the driver's internal rejoin loop, which keeps the link
            // status at CYW43_LINK_JOIN forever without ever reporting failure.
            if (now_ms - m_last_link_up_ms > WIFI_JOIN_WATCHDOG_MS)
            {
                LOG_WARNING("Link down for %lu ms; forcing Wi-Fi restart",
                            static_cast<unsigned long>(now_ms - m_last_link_up_ms));
                if (!m_command_sender.restart_wifi())
                    LOG_CRITICAL("Watchdog Wi-Fi restart failed");
                m_last_link_up_ms = to_ms_since_boot(get_absolute_time());
            }
        }

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
    // never accumulates toward the restart.
    if (p_sample.max_center_offset() > WIFI_RESTART_CENTER_TOLERANCE)
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
    if (now_ms - *m_button_hold_start_ms > WIFI_RESTART_HOLD_MS)
    {
        /*
         * Remote recovery button: bounce the car's Wi-Fi first, then our own after a short
         * drain delay. The car stops its motors before restarting, and the auto-arm in run()
         * re-establishes a fresh session once both links are back.
         */
        LOG_INFO("Wi-Fi restart gesture: restarting car, then controller");
        m_command_sender.send_wifi_restart();
        sleep_ms(LOCAL_WIFI_RESTART_DELAY_MS);
        m_session_started = false;
        if (!m_command_sender.restart_wifi())
            LOG_CRITICAL("Local Wi-Fi restart failed");

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
