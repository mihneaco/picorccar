#pragma once

#include <cstdint>
#include <optional>

#include "command_sender.h"
#include "joystick_controller.h"

class RemoteController
{
public:
    RemoteController(JoystickController& p_joystick_controller,
                     CommandSender&      p_command_sender);

    bool init();
    void run();

private:
    void handle_joystick_sample(const JoystickController::Sample& p_sample);
    void handle_joystick_button(const JoystickController::Sample& p_sample);
    void handle_joystick_position(const JoystickController::Sample& p_sample);

    JoystickController& m_joystick_controller;
    CommandSender& m_command_sender;

    bool m_initialized{};
    bool m_session_started{};

    bool m_fired{};
    std::optional<std::uint32_t> m_button_hold_start_ms{};

    std::optional<protocol::CtrlState> m_last_sent_controller_state{};
    std::uint32_t m_last_successful_send_ms{};
};
