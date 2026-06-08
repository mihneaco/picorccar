#include "joystick_controller.h"
#include "pinout.h"
#include "picorccar/pico_logger.h"
#include "command_sender.h"

#include "lwip/ip_addr.h"
#include "pico/stdlib.h"

namespace
{
constexpr char ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char ACCESS_POINT_PSK[] = PICORCCAR_ACCESS_POINT_PSK;
constexpr char UDP_SERVER_IP[] = PICORCCAR_UDP_SERVER_IP;
constexpr std::uint16_t UDP_SERVER_PORT = PICORCCAR_UDP_SERVER_PORT;
constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 1000;
}

int main()
{
    stdio_init_all();
    logger::init(LOGGING_THRESHOLD);

    LOG_INFO("Initializing Joystick Controller");
    JoystickController joystick_controller(pinout::JOYSTICK_PINS);
    if (!joystick_controller.init())
    {
        LOG_CRITICAL("Joystick controller initialization failed");
        while (true)
            tight_loop_contents();
    }

    ip_addr_t remote_address{};
    if (!ipaddr_aton(UDP_SERVER_IP, &remote_address))
    {
        LOG_CRITICAL("Failed to parse UDP server IP '%s'", UDP_SERVER_IP);
        while (true)
            tight_loop_contents();
    }

    LOG_INFO("Initializing Command Sender");
    CommandSender command_sender(ACCESS_POINT_SSID,
                                 ACCESS_POINT_PSK,
                                 remote_address,
                                 UDP_SERVER_PORT);
    if (!command_sender.init())
    {
        LOG_CRITICAL("Command sender initialization failed");
        while (true)
            tight_loop_contents();
    }

    LOG_INFO("Controller firmware placeholder started");

    while (true)
    {
        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }

    return 0;
}
