#include "network_conf.h"
#include "joystick_controller.h"
#include "pinout.h"
#include "pico_logger.h"
#include "command_sender.h"

#include "lwip/ip_addr.h"
#include "pico/stdlib.h"

namespace
{
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
    if (!ipaddr_aton(common::UDP_SERVER_IP, &remote_address))
    {
        LOG_CRITICAL("Failed to parse UDP server IP '%s'", common::UDP_SERVER_IP);
        while (true)
            tight_loop_contents();
    }

    LOG_INFO("Initializing Command Sender");
    CommandSender command_sender(common::ACCESS_POINT_SSID,
                                 common::ACCESS_POINT_PSK,
                                 remote_address,
                                 common::UDP_SERVER_PORT);
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
