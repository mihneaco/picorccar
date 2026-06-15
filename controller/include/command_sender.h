#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
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
    bool start_new_session();
    bool send_controller_state(const protocol::CtrlState& p_ctrl_state);

private:
    bool send_packet(const protocol::RCCarPacket& p_packet);
    bool send_packet_bytes(const void* p_payload, std::size_t p_length);
    void cleanup();

    const char* m_access_point_ssid;
    const char* m_access_point_password;
    const char* m_remote_address_string;
    ip_addr_t m_remote_address{};
    std::uint16_t m_remote_port;

    udp_pcb* m_udp_pcb = nullptr;
    std::uint32_t m_session_id = 0;
    bool m_initialized = false;
    bool m_cyw43_initialized = false;
    bool m_session_active = false;
};
