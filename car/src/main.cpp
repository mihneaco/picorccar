#include <cstdint>

#include "car_controller.h"
#include "command_receiver.h"
#include "motor_driver.h"

#include "picorccar/logger.h"

#include "pico/stdlib.h"

namespace
{
constexpr char ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char ACCESS_POINT_PSK[] = PICORCCAR_ACCESS_POINT_PSK;
constexpr std::uint16_t UDP_SERVER_PORT = PICORCCAR_UDP_SERVER_PORT;
#ifdef PICORCCAR_DEBUG
constexpr std::uint32_t DEBUG_STARTUP_LOG_DELAY_MS = 3000;
#endif
}

int main()
{
    /*
        TB6612FNG has an internal pull-down that holds STBY low at MCU startup/reboot.
        It's fine to init logging first to benefit from it inside the motor driver code.
    */
    stdio_init_all();
    logger::init(LOGGING_THRESHOLD);
#ifdef PICORCCAR_DEBUG
    // Delay to allow opening a serial connection for debug logs.
    sleep_ms(DEBUG_STARTUP_LOG_DELAY_MS);
#endif
    LOG_INFO("Logging initialized");

    CommandReceiver command_receiver(ACCESS_POINT_SSID, ACCESS_POINT_PSK, UDP_SERVER_PORT);
    MotorDriver motor_driver(pinout::MOTOR_DRIVER_PINS);
    CarController car_controller(command_receiver, motor_driver, CarController::ConfigHalfDuty);

    if (!car_controller.init())
    {
        while (true)
            tight_loop_contents();
    }

    car_controller.run();
    return 0;
}
