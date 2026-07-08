#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

#include "lwip/ip_addr.h"
#include "picorccar/protocol.h"

struct udp_pcb;

class CommandSender
{
public:
    CommandSender(const char* p_access_point_ssid,
                  const char* p_access_point_password,
                  const char* p_remote_address,
                  std::uint16_t p_remote_port);
    ~CommandSender();

    CommandSender(const CommandSender& p_other) = delete;
    CommandSender(CommandSender&& p_other) = delete;
    CommandSender& operator=(const CommandSender& p_other) = delete;
    CommandSender& operator=(CommandSender&& p_other) = delete;

    bool init();
    bool connect();
    bool is_connected();
    bool start_new_session();
    bool end_session();
    bool send_controller_state(const protocol::CtrlState& p_ctrl_state);
    bool send_wifi_restart();
    bool restart_wifi();
    /**
     * @brief Read the RSSI in dBm of the link to the car's AP.
     * @return std::nullopt when the STA link is down or the query fails.
     * @note Issues a blocking CYW43 ioctl; call at a low rate from the main loop only.
     */
    std::optional<std::int32_t> read_rssi();

private:
    bool init_wifi();
    bool send_session_control(protocol::RCCarPacket::ArmFlag p_arm_flag);
    bool send_packet_repeated(protocol::RCCarPacket& p_packet);
    bool send_packet(const protocol::RCCarPacket& p_packet);
    bool send_packet_bytes(const void* p_payload, std::size_t p_length);
    void cleanup();

    const char* m_access_point_ssid{};
    const char* m_access_point_password{};
    const char* m_remote_address_string{};
    ip_addr_t m_remote_address{};
    std::uint16_t m_remote_port{};

    udp_pcb* m_udp_pcb{};
    std::uint32_t m_session_id{};

    bool m_initialized{};
    bool m_wifi_initialized{};
    bool m_session_active{};

    std::uint32_t m_last_conn_attempt_ms{};
};
