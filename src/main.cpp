#include <cstdint>

#include "pico_logger.h"
#include "motor_driver.h"
#include "remote_link.h"

// Pico SDK
#include "lwip/ip_addr.h"
#include "pico/stdlib.h"

namespace
{
// TB6612FNG pin mapping
constexpr MotorDriver::Pin PWMA = 2;
constexpr MotorDriver::Pin AIN2 = 3;
constexpr MotorDriver::Pin AIN1 = 4;
constexpr MotorDriver::Pin STBY = 5;
constexpr MotorDriver::Pin BIN1 = 6;
constexpr MotorDriver::Pin BIN2 = 7;
constexpr MotorDriver::Pin PWMB = 8;

constexpr MotorDriver::DriverPins MOTOR_DRIVER_PINS{
    {AIN1, AIN2, PWMA},
    {BIN1, BIN2, PWMB},
    STBY
};

/* 
    @todo read AP credentials from file.
    @todo change the passwd if you reuse this.
*/
constexpr char ACCESS_POINT_SSID[] = "PicoRCCar";
constexpr char ACCESS_POINT_PSK[] = "brick-owl-69";
constexpr std::uint16_t UDP_SERVER_PORT = 12345;

constexpr std::uint32_t MAIN_LOOP_SLEEP_MS = 20;
constexpr std::uint32_t COMMAND_TIMEOUT_MS = 250;

void print_udp_packet(const RemoteLink::Packet& p_packet)
{
    char remote_address[IPADDR_STRLEN_MAX]{};
    ipaddr_ntoa_r(&p_packet.remote_address, remote_address, sizeof(remote_address));

    if (p_packet.truncated)
    {
        LOG_TRACE("UDP packet from %s:%u len=%u copied=%u overwritten=%lu",
                  remote_address,
                  static_cast<unsigned>(p_packet.remote_port),
                  static_cast<unsigned>(p_packet.total_length),
                  static_cast<unsigned>(p_packet.copied_length),
                  static_cast<unsigned long>(p_packet.overwritten_packets));
    }
    else
    {
        LOG_TRACE("UDP packet from %s:%u len=%u overwritten=%lu",
                  remote_address,
                  static_cast<unsigned>(p_packet.remote_port),
                  static_cast<unsigned>(p_packet.total_length),
                  static_cast<unsigned long>(p_packet.overwritten_packets));
    }
}
}

int main()
{
    /*
        TB6612FNG has an internal pull-down that holds STBY low at MCU startup/reboot.
          It's fine to init logging first to benefit from it inside the motor driver code.
    */
    stdio_init_all();
    logger::init(LOGGING_THRESHOLD);
    /*
        @description Delay to allow opening of a serial conn to read the logs.
        @todo Remove this or move under ifdef DEBUG.
    */
    sleep_ms(3000);
    LOG_INFO("Logging initialized");

    LOG_INFO("Initializing Motor Driver");
    MotorDriver motor_driver(MOTOR_DRIVER_PINS);
    motor_driver.init();
    motor_driver.stop_all();

    LOG_INFO("Initializing Remote Link");
    RemoteLink remote_link(ACCESS_POINT_SSID, ACCESS_POINT_PSK, UDP_SERVER_PORT);
    if (!remote_link.init())
    {
        LOG_CRITICAL("Remote link initialization failed");
        motor_driver.stop_all();
        while (true)
            tight_loop_contents();
    }

    // #### MAIN LOOP ####
    LOG_INFO("Starting MAIN loop");
    RemoteLink::Packet latest_packet{};
    std::uint32_t last_packet_ms{to_ms_since_boot(get_absolute_time())};
    while (true)
    {
        /*
            @todo - Add motor-command decoding, command timeout and failsafe updates.
        */

        auto res = remote_link.get_packet(latest_packet);
        if (res)
        {
            last_packet_ms = latest_packet.received_ms;
            print_udp_packet(latest_packet);
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
