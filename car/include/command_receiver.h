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
    void reset_session();

private:
    void receive_callback(pbuf* p_packet);
    void cleanup();

    const char* m_access_point_ssid;
    const char* m_access_point_password;

    std::uint16_t m_server_port;

    udp_pcb* m_udp_pcb{};
    critical_section_t m_packet_lock{};
    ReceivedCommand m_received_command{};
    // @details Use only with m_packet_lock locked.
    bool m_has_packet{};
    std::uint32_t m_active_session_id{};
    std::uint32_t m_pending_session_id{};
    std::uint8_t m_pending_hello_count{};
    bool m_session_armed{};
    bool m_initialized{};
    bool m_cyw43_initialized{};
};
