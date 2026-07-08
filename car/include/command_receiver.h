#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "pico/critical_section.h"

#include "picorccar/protocol.h"

struct pbuf;
struct udp_pcb;

class CommandReceiver
{
public:
    struct ReceivedCommand
    {
        protocol::CtrlState m_ctrl_state   {};
        std::uint32_t       m_received_ms  {};
        std::uint32_t       m_sent_ms      {};
    };

    CommandReceiver(const char* p_access_point_ssid,
                    const char* p_access_point_password,
                    std::uint16_t p_port);
    ~CommandReceiver();

    CommandReceiver(const CommandReceiver& p_other)            = delete;
    CommandReceiver(CommandReceiver&& p_other)                 = delete;
    CommandReceiver& operator=(const CommandReceiver& p_other) = delete;
    CommandReceiver& operator=(CommandReceiver&& p_other)      = delete;

    bool init();
    bool get_packet(ReceivedCommand& p_received_command);
    bool restart_wifi();
    bool consume_restart_request();
    /**
     * @brief Read the RSSI in dBm of the first associated station (the controller).
     * @return std::nullopt when no station is associated or the query fails.
     * @note Issues blocking CYW43 ioctls; call at a low rate from the main loop only,
     *       never from a callback.
     */
    std::optional<std::int32_t> read_client_rssi();

private:
    bool init_wifi();
    bool init_server();
    void receive_callback(pbuf* p_packet);
    void handle_arm_packet(const std::uint8_t* p_payload, std::uint32_t p_session_id);
    void handle_com_packet(const std::uint8_t* p_payload, std::uint32_t p_session_id);
    void handle_rst_packet(std::uint32_t p_session_id);
    void cleanup();

    const char* m_access_point_ssid;
    const char* m_access_point_password;

    std::uint16_t m_server_port;

    udp_pcb* m_udp_pcb{};
    critical_section_t m_packet_lock{};
    ReceivedCommand m_received_command{};
    /// @note Use only with m_packet_lock locked.
    bool m_has_packet{};
    /// @note Use only with m_packet_lock locked.
    bool m_restart_requested{};
    std::uint32_t m_active_session_id{};
    bool m_session_armed{};
    bool m_initialized{};
    bool m_wifi_initialized{};
};
