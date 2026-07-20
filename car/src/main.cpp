#include <cstdint>

#include "car_controller.h"
#include "command_receiver.h"
#include "motor_driver.h"

#include "picorccar/logger.h"

#include "pico/stdlib.h"

namespace
{
constexpr char          ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char          ACCESS_POINT_PSK[]  = PICORCCAR_ACCESS_POINT_PSK;
constexpr std::uint16_t UDP_SERVER_PORT     = PICORCCAR_UDP_SERVER_PORT;
}

int main()
{
    /*
        TB6612FNG has an internal pull-down that holds STBY low at MCU startup/reboot.
        It's fine to init logging first to benefit from it inside the motor driver code.
    */
    stdio_init_all();
    logger::init(LOGGING_THRESHOLD);
    LOG_INFO("Logging initialized");

    CommandReceiver command_receiver(ACCESS_POINT_SSID, ACCESS_POINT_PSK, UDP_SERVER_PORT);
    MotorDriver motor_driver(pinout::MOTOR_DRIVER_PINS);
    CarController car_controller(command_receiver, motor_driver, CarController::ACTIVE_CONFIG);

    if (!car_controller.init())
    {
        while (true)
            tight_loop_contents();
    }

    car_controller.run();
    return 0;
}
