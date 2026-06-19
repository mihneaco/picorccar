#pragma once

#include "command_receiver.h"
#include "motor_driver.h"

class Main
{
public:
    Main();

    int run();

private:
    CommandReceiver m_command_receiver;
    MotorDriver m_motor_driver;
};
