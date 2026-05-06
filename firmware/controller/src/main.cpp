#include "pico_logger.h"

#include "pico/stdlib.h"

int main()
{
    stdio_init_all();
    logger::init(LOGGING_THRESHOLD);
    LOG_INFO("Controller firmware placeholder started");

    while (true)
    {
        sleep_ms(1000);
    }

    return 0;
}
