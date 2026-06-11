#include "joystick_controller.h"
#include "pinout.h"
#include "command_sender.h"

#include "picorccar/logger.h"
#include "picorccar/protocol.h"

#include <cstring>

#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include "pico/stdlib.h"
#include "pico/rand.h"

namespace
{
constexpr char          ACCESS_POINT_SSID[] = PICORCCAR_ACCESS_POINT_SSID;
constexpr char          ACCESS_POINT_PSK[]  = PICORCCAR_ACCESS_POINT_PSK;
constexpr char          UDP_SERVER_IP[]     = PICORCCAR_UDP_SERVER_IP;
constexpr std::uint16_t UDP_SERVER_PORT     = PICORCCAR_UDP_SERVER_PORT;

constexpr std::size_t CTRL_STATE_X_AXIS_OFFSET = 0;
constexpr std::size_t CTRL_STATE_Y_AXIS_OFFSET = CTRL_STATE_X_AXIS_OFFSET + sizeof(std::uint16_t);
constexpr std::uint32_t HELLO_PACKET_SPACING_MS = 20;
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
    std::uint32_t sessionId = get_rand_32();
    protocol::RCCarPacket hello_packet{};
    hello_packet.m_mode = protocol::RCCarPacket::Mode::HELLO;
    hello_packet.m_session_id = sessionId;
    for (std::uint8_t hello_packet_index = 0;
         hello_packet_index < protocol::SESSION_HELLO_PACKET_COUNT;
         ++hello_packet_index)
    {
        hello_packet.m_session_ms = to_ms_since_boot(get_absolute_time());
        command_sender.send_packet(hello_packet);
        sleep_ms(HELLO_PACKET_SPACING_MS);
    }

    LOG_INFO("Starting Controller MAIN LOOP");
    // MAIN LOOP
    JoystickController::Sample joystick_sample{};
    while (true)
    {
        if (joystick_controller.read(joystick_sample))
        {
            protocol::RCCarPacket command_packet{};
            command_packet.m_mode = protocol::RCCarPacket::Mode::COMMAND;
            command_packet.m_session_id = sessionId;
            command_packet.m_session_ms = to_ms_since_boot(get_absolute_time());

            const std::uint16_t x_axis_be = lwip_htons(joystick_sample.m_x_axis);
            std::memcpy(&command_packet.m_payload[CTRL_STATE_X_AXIS_OFFSET], &x_axis_be, sizeof(x_axis_be));

            const std::uint16_t y_axis_be = lwip_htons(joystick_sample.m_y_axis);
            std::memcpy(&command_packet.m_payload[CTRL_STATE_Y_AXIS_OFFSET], &y_axis_be, sizeof(y_axis_be));

            command_sender.send_packet(command_packet);
        }

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }

    return 0;
}
