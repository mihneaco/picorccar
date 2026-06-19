#pragma once

#include <optional>

#include "command_sender.h"
#include "joystick_controller.h"

class Main
{
public:
    Main();

    int run();

private:
    void handle_joystick_sample(const JoystickController::Sample& p_sample);
    void handle_joystick_button(bool p_button_pressed);
    void handle_joystick_position(const JoystickController::Sample& p_sample);

    CommandSender m_command_sender;
    JoystickController m_joystick_controller;
    bool m_session_started = false;
    bool m_was_button_pressed = false;
    std::optional<protocol::CtrlState> m_last_sent_controller_state;
    std::uint32_t m_last_successful_send_ms = 0;
};
