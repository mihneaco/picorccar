#include <cstdint>

#include "main.h"

#include "picorccar/logger.h"
#include "picorccar/protocol.h"

#include "pico/stdlib.h"

namespace
{
constexpr char ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char ACCESS_POINT_PSK[] = PICORCCAR_ACCESS_POINT_PSK;
constexpr std::uint16_t UDP_SERVER_PORT = PICORCCAR_UDP_SERVER_PORT;
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;

void print_packet(const CommandReceiver::ReceivedCommand& p_received_command)
{
    LOG_TRACE("UDP packet x=%u y=%u sent_ms=%u received_ms=%u",
              static_cast<unsigned>(p_received_command.m_ctrl_state.m_x_axis),
              static_cast<unsigned>(p_received_command.m_ctrl_state.m_y_axis),
              static_cast<unsigned>(p_received_command.m_sent_ms),
              static_cast<unsigned>(p_received_command.m_received_ms));

    // @todo DELETE THIS. Debug pourposes so as not to spam with logging.
    sleep_ms(500);
}
}

Main::Main()
    : m_command_receiver(ACCESS_POINT_SSID, ACCESS_POINT_PSK, UDP_SERVER_PORT),
      m_motor_driver(pinout::MOTOR_DRIVER_PINS)
{
}

int Main::run()
{
    LOG_INFO("Initializing Motor Driver");
    m_motor_driver.init();
    m_motor_driver.stop_all();

    LOG_INFO("Initializing Command Receiver");
    if (!m_command_receiver.init())
    {
        LOG_CRITICAL("Command receiver initialization failed");
        m_motor_driver.set_standby(false);
        while (true)
            tight_loop_contents();
    }

    // #### MAIN LOOP ####
    LOG_INFO("Starting MAIN loop");
    CommandReceiver::ReceivedCommand latest_received_command{};
    std::uint32_t last_packet_ms{to_ms_since_boot(get_absolute_time())};
    bool command_timed_out{false};
    while (true)
    {
        auto res = m_command_receiver.get_packet(latest_received_command);
        if (res)
        {
            last_packet_ms = latest_received_command.m_received_ms;
            command_timed_out = false;
            print_packet(latest_received_command);
        }

        const std::uint32_t packet_age_ms = to_ms_since_boot(get_absolute_time()) - last_packet_ms;
        if (!command_timed_out && packet_age_ms > (2u * protocol::KEEP_ALIVE_MS))
        {
            m_motor_driver.stop_all();
            m_command_receiver.reset_session();
            command_timed_out = true;
        }

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }

    return 0;
}

int main()
{
    /*
        @note TB6612FNG has an internal pull-down that holds STBY low at MCU startup/reboot.
                It's fine to init logging first to benefit from it inside the motor driver code.
    */
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
