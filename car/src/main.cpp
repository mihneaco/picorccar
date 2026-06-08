#include <cstdint>

#include "picorccar/pico_logger.h"
#include "motor_driver.h"
#include "command_receiver.h"

// Pico SDK
#include "pico/stdlib.h"

namespace
{
constexpr char ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char ACCESS_POINT_PSK[] = PICORCCAR_ACCESS_POINT_PSK;
constexpr std::uint16_t UDP_SERVER_PORT = PICORCCAR_UDP_SERVER_PORT;
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;
constexpr std::uint32_t COMMAND_TIMEOUT_MS = 250;

void print_udp_packet(const CommandReceiver::ReceivedCommand& p_received_command)
{
    LOG_TRACE("UDP packet sent_ms=%lu",
              static_cast<unsigned long>(p_received_command.sent_ms));
}
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

    LOG_INFO("Initializing Motor Driver");
    MotorDriver motor_driver(pinout::MOTOR_DRIVER_PINS);
    motor_driver.init();
    motor_driver.stop_all();

    LOG_INFO("Initializing Command Receiver");
    CommandReceiver command_receiver(ACCESS_POINT_SSID,
                                     ACCESS_POINT_PSK,
                                     UDP_SERVER_PORT);
    if (!command_receiver.init())
    {
        LOG_CRITICAL("Command receiver initialization failed");
        motor_driver.set_standby(false);
        while (true)
            tight_loop_contents();
    }

    // #### MAIN LOOP ####
    LOG_INFO("Starting MAIN loop");
    CommandReceiver::ReceivedCommand latest_received_command{};
    std::uint32_t last_packet_ms{to_ms_since_boot(get_absolute_time())};
    while (true)
    {
        /*
            @todo - Add motor-command decoding, command timeout and failsafe updates.
        */

        auto res = command_receiver.get_packet(latest_received_command);
        if (res)
        {
            last_packet_ms = latest_received_command.received_ms;
            print_udp_packet(latest_received_command);
        }

        const std::uint32_t packet_age_ms = to_ms_since_boot(get_absolute_time()) - last_packet_ms;
        if (packet_age_ms > COMMAND_TIMEOUT_MS)
        {
            /* @todo uncomment after adding command packet processing
                motor_driver.stop_all();
            */
        }

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }

    return 0;
}
