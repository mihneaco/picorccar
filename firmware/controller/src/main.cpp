#include "ble/ble_conf.h"
#include "joystick_controller.h"
#include "pinout.h"
#include "pico_logger.h"
#include "remote_link.h"

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

    LOG_INFO("Initializing Remote Link");
    RemoteLink remote_link(common::BLE_DEVICE_NAME);
    if (!remote_link.init())
    {
        LOG_CRITICAL("Remote link initialization failed");
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
