#include "main.h"

#include "picorccar/logger.h"
#include "picorccar/protocol.h"

#include "pico/stdlib.h"

namespace
{
constexpr char          ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char          ACCESS_POINT_PSK[]  = PICORCCAR_ACCESS_POINT_PSK;
constexpr char          UDP_SERVER_IP[]     = PICORCCAR_UDP_SERVER_IP;
constexpr std::uint16_t UDP_SERVER_PORT     = PICORCCAR_UDP_SERVER_PORT;

constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;
}

Main::Main()
    : m_command_sender(ACCESS_POINT_SSID,
                       ACCESS_POINT_PSK,
                       UDP_SERVER_IP,
                       UDP_SERVER_PORT),
      m_joystick_controller(pinout::JOYSTICK_PINS)
{
}

int Main::run()
{
    LOG_INFO("Initializing Joystick Controller");
    if (!m_joystick_controller.init())
    {
        LOG_CRITICAL("Joystick controller initialization failed");
        while (true)
            tight_loop_contents();
    }

    LOG_INFO("Initializing Command Sender");
    if (!m_command_sender.init())
    {
        LOG_CRITICAL("Command sender initialization failed");
        while (true)
            tight_loop_contents();
    }

    // MAIN LOOP
    LOG_INFO("Starting Controller MAIN LOOP");
    JoystickController::Sample joystick_sample{};
    while (true)
    {
        if (m_joystick_controller.read(joystick_sample))
            handle_joystick_sample(joystick_sample);

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }

    return 0;
}

void Main::handle_joystick_sample(const JoystickController::Sample& p_sample)
{
    handle_joystick_button(p_sample.m_bpressed);
    handle_joystick_position(p_sample);
}

void Main::handle_joystick_button(const bool p_button_pressed)
{
    if (p_button_pressed == m_was_button_pressed)
        return;

    LOG_INFO("Joystick button %s", p_button_pressed ? "pressed" : "released");
    m_was_button_pressed = p_button_pressed;

    if (p_button_pressed == false)
        return;

    m_session_started = m_command_sender.start_new_session();
    m_last_sent_controller_state.reset();
    m_last_successful_send_ms = 0;
    LOG_INFO("Button-triggered session restart %s",
             m_session_started ? "succeeded" : "failed");
}

void Main::handle_joystick_position(const JoystickController::Sample& p_sample)
{
    if (!m_session_started)
        return;

    const protocol::CtrlState controller_state {
        p_sample.m_x_axis,
        p_sample.m_y_axis};
    const std::uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    const bool keep_alive_due =
        m_last_sent_controller_state.has_value() &&
        (now_ms - m_last_successful_send_ms) >= protocol::KEEP_ALIVE_MS;

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

int main()
{
    stdio_init_all();
    logger::init(LOGGING_THRESHOLD);
    /*
        @note Delay to allow opening of a serial conn to read the logs.
        @todo Remove this or move under ifdef DEBUG.
    */
    sleep_ms(3000);
    LOG_INFO("Logging initialized");

    Main main_app;
    return main_app.run();
}
