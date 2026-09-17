#pragma once

#include <cstdint>
#include <optional>

#include "command_sender.h"
#include "joystick_controller.h"

class RemoteController
{
public:
    /** @brief Bind the joystick and sender to run against. */
    RemoteController(JoystickController& p_joystick_controller, CommandSender& p_command_sender);

    /** @brief Init the joystick and command sender. */
    bool init();
    /** @brief Main loop: connect/reconnect, arm sessions, and stream joystick state. */
    void run();

private:
    /** @brief Dispatch one joystick sample to the button-hold and position handlers. */
    void handle_joystick_sample(const JoystickController::Sample& p_sample);
    /** @brief Track the Wi-Fi-restart button-hold gesture and trigger the restart. */
    void handle_joystick_button(const JoystickController::Sample& p_sample);
    /** @brief Send the joystick position if it changed or a keep-alive is due. */
    void handle_joystick_position(const JoystickController::Sample& p_sample);

    JoystickController& m_joystick_controller;
    CommandSender& m_command_sender;

    bool m_initialized{};
    bool m_session_started{};

    bool m_fired{};
    std::optional<std::uint32_t> m_button_hold_start_ms{};

    std::optional<protocol::CtrlState> m_last_sent_controller_state{};
    std::uint32_t m_last_successful_send_ms{};

    /**
     * @brief Last time the link was seen connected, for the join watchdog.
     * @details The CYW43 driver can loop a doomed join internally forever (edge-of-range
     *          WPA2 handshake timeouts keep the link status at CYW43_LINK_JOIN without ever
     *          reporting failure), so connect() alone never escalates. If the link stays
     *          down past the watchdog deadline, run() forces a full Wi-Fi restart.
     */
    std::uint32_t m_last_link_up_ms{};
};
