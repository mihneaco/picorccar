#include "pico_logger.h"
#include "motor_driver.h"
#include "udp_server.h"

// Pico SDK
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

constexpr uint16_t BRINGUP_TEST_SPEED = MotorDriver::MAX_SPEED;
constexpr uint32_t BRINGUP_RUN_MS = 750;
constexpr uint32_t BRINGUP_PAUSE_MS = 750;

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

static void test_bringup()
{
    LOG_INFO();

    MotorDriver motor_driver(MOTOR_DRIVER_PINS);
    motor_driver.init();

    // Pico 2 W exposes the onboard LED through the CYW43 chip; initialize the
    // CYW43 architecture once at the visible startup call site.
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43 init failed: %d", cyw43_init_result);
        while (true)
            tight_loop_contents();
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);

    using Direction = MotorDriver::Direction;

    while (true)
    {
        // Motor A
        motor_driver.set_motor_a(Direction::Forward, BRINGUP_TEST_SPEED);
        motor_driver.set_motor_b(Direction::Stop, 0);
        sleep_ms(BRINGUP_RUN_MS);

        // Stop
        motor_driver.stop_all();
        sleep_ms(BRINGUP_PAUSE_MS);

        // Motor B
        motor_driver.set_motor_a(Direction::Stop, 0);
        motor_driver.set_motor_b(Direction::Forward, BRINGUP_TEST_SPEED);
        sleep_ms(BRINGUP_RUN_MS);

        // Stop Both
        motor_driver.stop_all();
        sleep_ms(BRINGUP_PAUSE_MS);

        // Together
        motor_driver.set_motor_a(Direction::Forward, BRINGUP_TEST_SPEED);
        motor_driver.set_motor_b(Direction::Forward, BRINGUP_TEST_SPEED);
        sleep_ms(BRINGUP_RUN_MS);

        motor_driver.stop_all();
        sleep_ms(BRINGUP_PAUSE_MS * 2);
    }
}

static constexpr char ACCESS_POINT_SSID[] = "PicoRCCar";
// Empty password keeps the access point open, so any Wi-Fi client can attach.
static constexpr char ACCESS_POINT_PSK[] = "";
static constexpr char UDP_SERVER_IP[] = "192.168.4.1";
static constexpr uint16_t UDP_SERVER_PORT = 12345;
static constexpr uint32_t MAIN_LOOP_SLEEP_MS = 10;

int main()
{
    stdio_init_all();
    logger::init(ULOG_TRACE_LEVEL);
    LOG_INFO();

    MotorDriver motor_driver(MOTOR_DRIVER_PINS);
    motor_driver.init();
    motor_driver.stop_all();

    UDPServer udp_server(ACCESS_POINT_SSID, ACCESS_POINT_PSK, UDP_SERVER_IP, UDP_SERVER_PORT);
    if (!udp_server.init())
    {
        LOG_CRITICAL("UDP server initialization failed");
        motor_driver.stop_all();
        while (true)
            tight_loop_contents();
    }

    LOG_INFO("main loop started");
    while (true)
    {
        udp_server.poll();

        // Future motor-command decoding, command timeout, and failsafe updates
        // should run here rather than from the lwIP receive callback.

        sleep_ms(MAIN_LOOP_SLEEP_MS);
    }

    return 0;
}
