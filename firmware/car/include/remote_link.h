#pragma once

#include <cstddef>
#include <cstdint>

#include "lwip/ip_addr.h"
#include "pico/critical_section.h"

struct pbuf;
struct udp_pcb;

class RemoteLink
{
public:
    static constexpr std::size_t MAX_PACKET_BYTES = 256;

    struct Packet
    {
        ip_addr_t remote_address{};
        std::uint16_t remote_port = 0;
        std::uint32_t received_ms = 0;
        std::uint32_t overwritten_packets = 0;
        std::uint16_t total_length = 0;
        std::uint16_t copied_length = 0;
        std::uint8_t payload[MAX_PACKET_BYTES]{};
        bool truncated = false;
    };

    RemoteLink(const char* p_access_point_ssid,
               const char* p_access_point_password,
               std::uint16_t p_port);
    ~RemoteLink();

    RemoteLink(const RemoteLink& p_other) = delete;
    RemoteLink(RemoteLink&& p_other) = delete;
    RemoteLink& operator=(const RemoteLink& p_other) = delete;
    RemoteLink& operator=(RemoteLink&& p_other) = delete;

    bool init();
    bool get_packet(Packet& p_packet);

private:
    void receive_callback(udp_pcb* p_pcb,
                          pbuf* p_packet,
                          const ip_addr_t* p_remote_address,
                          std::uint16_t p_remote_port);
    void cleanup();

    const char* m_access_point_ssid;
    const char* m_access_point_password;

    std::uint16_t m_server_port;

    udp_pcb* m_udp_pcb = nullptr;
    critical_section_t m_packet_lock{};
    Packet m_packet{};
    std::uint32_t m_overwritten_packets = 0;
    // @details Use only with m_packet_lock locked
    bool m_has_packet = false;
    bool m_initialized = false;
    bool m_cyw43_initialized = false;
};
