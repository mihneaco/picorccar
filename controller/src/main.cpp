#include <cstdint>

#include "command_sender.h"
#include "joystick_controller.h"
#include "remote_controller.h"

#include "picorccar/logger.h"

#include "pico/stdlib.h"

namespace
{
constexpr char          ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char          ACCESS_POINT_PSK[]  = PICORCCAR_ACCESS_POINT_PSK;
constexpr char          UDP_SERVER_IP[]     = PICORCCAR_UDP_SERVER_IP;
constexpr std::uint16_t UDP_SERVER_PORT     = PICORCCAR_UDP_SERVER_PORT;
}

int main()
{
    stdio_init_all();
    logger::init(LOGGING_THRESHOLD);
    LOG_INFO("Logging initialized");

    CommandSender command_sender(ACCESS_POINT_SSID,
                                 ACCESS_POINT_PSK,
                                 UDP_SERVER_IP,
                                 UDP_SERVER_PORT);
    JoystickController joystick_controller(pinout::JOYSTICK_PINS);
    RemoteController remote_controller(joystick_controller, command_sender);

    if (!remote_controller.init())
    {
        while (true)
            tight_loop_contents();
    }

    remote_controller.run();
    return 0;
}
