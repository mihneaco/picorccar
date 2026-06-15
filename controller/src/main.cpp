#include "joystick_controller.h"
#include "pinout.h"
#include "command_sender.h"

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

    LOG_INFO("Initializing Command Sender");
    CommandSender command_sender(ACCESS_POINT_SSID,
                                 ACCESS_POINT_PSK,
                                 UDP_SERVER_IP,
                                 UDP_SERVER_PORT);
    if (!command_sender.init())
    {
        LOG_CRITICAL("Command sender initialization failed");
        while (true)
            tight_loop_contents();
    }

    // MAIN LOOP
    LOG_INFO("Starting Controller MAIN LOOP");
    JoystickController::Sample joystick_sample{};
    command_sender.start_new_session();
    bool was_button_pressed = false;
    while (true)
    {
        if (joystick_controller.read(joystick_sample))
        {
            if (joystick_sample.m_bpressed != was_button_pressed)
            {
                if (joystick_sample.m_bpressed == true)
                    command_sender.start_new_session();

                was_button_pressed = joystick_sample.m_bpressed;
            }

            const protocol::CtrlState controller_state {
                joystick_sample.m_x_axis,
                joystick_sample.m_y_axis};
            command_sender.send_controller_state(controller_state);
        }

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }

    return 0;
}
