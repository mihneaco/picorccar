#pragma once

#include <cstddef>
#include <cstdint>

#include "pico/critical_section.h"

#include "picorccar/protocol.h"

struct pbuf;
struct udp_pcb;

class CommandReceiver
{
public:
    struct ReceivedCommand
    {
        protocol::CtrlState m_ctrl_state{};
        std::uint32_t m_received_ms{};
        std::uint32_t m_sent_ms{};
    };

    /** @brief Store AP credentials and UDP port; does not init hardware. */
    CommandReceiver(const char* p_access_point_ssid,
                    const char* p_access_point_password,
                    std::uint16_t p_port);
    /** @brief Tear down Wi-Fi/UDP if initialized. */
    ~CommandReceiver();

    CommandReceiver(const CommandReceiver& p_other) = delete;
    CommandReceiver(CommandReceiver&& p_other) = delete;
    CommandReceiver& operator=(const CommandReceiver& p_other) = delete;
    CommandReceiver& operator=(CommandReceiver&& p_other) = delete;

    /** @brief Bring up the AP and start listening for UDP packets. */
    bool init();
    /** @brief Fetch the latest received command, if any. */
    bool get_packet(ReceivedCommand& p_received_command);
    /** @brief Tear down and reinit Wi-Fi and the UDP server. */
    bool restart_wifi();
    /** @brief Consume and clear a pending restart request set by the receive path. */
    bool consume_restart_request();

private:
    /** @brief Bring up the CYW43 chip and start the AP. */
    bool init_wifi();
    /** @brief Open the UDP socket and register the receive callback. */
    bool init_server();
    /** @brief lwIP callback: dispatch an incoming packet by mode. */
    void receive_callback(pbuf* p_packet);
    /** @brief Handle an ARM/DISARM session-control packet. */
    void handle_arm_packet(const std::uint8_t* p_payload, std::uint32_t p_session_id);
    /** @brief Handle a COM (joystick state) packet for the active session. */
    void handle_com_packet(const std::uint8_t* p_payload, std::uint32_t p_session_id);
    /** @brief Handle an RST packet: flag a Wi-Fi restart if the session matches. */
    void handle_rst_packet(std::uint32_t p_session_id);
    /** @brief Release the UDP socket and CYW43 resources. */
    void cleanup();

    const char* m_access_point_ssid;
    const char* m_access_point_password;

    std::uint16_t m_server_port;

    udp_pcb* m_udp_pcb{};
    critical_section_t m_packet_lock{};
    ReceivedCommand m_received_command{};
    /** @note Use only with m_packet_lock locked. */
    bool m_has_packet{};
    /** @note Use only with m_packet_lock locked. */
    bool m_restart_requested{};
    std::uint32_t m_active_session_id{};
    bool m_session_armed{};
    bool m_initialized{};
    bool m_wifi_initialized{};
};
