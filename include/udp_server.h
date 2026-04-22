#pragma once

#include <cstddef>
#include <cstdint>

#include "lwip/ip_addr.h"
#include "pico/critical_section.h"

struct pbuf;
struct udp_pcb;

class UDPServer
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

    UDPServer(const char* access_point_ssid,
              const char* access_point_password,
              const char* server_ip,
              std::uint16_t port);
    ~UDPServer();

    UDPServer(const UDPServer&) = delete;
    UDPServer& operator=(const UDPServer&) = delete;

    bool init();
    bool take_latest_packet(Packet& packet);
    void poll();

private:
    void receive_callback(udp_pcb* pcb, pbuf* packet, const ip_addr_t* remote_address, std::uint16_t remote_port);
    void print_packet(const Packet& packet);
    void print_ascii_payload(const Packet& packet);
    void print_hex_payload(const Packet& packet);
    void cleanup();

    const char* m_access_point_ssid;
    const char* m_access_point_password;
    const char* m_server_ip;
    std::uint16_t m_port;
    udp_pcb* m_udp_pcb = nullptr;
    critical_section_t m_packet_lock{};
    Packet m_latest_packet{};
    std::uint32_t m_overwritten_packets = 0;
    bool m_has_latest_packet = false;
    bool m_cyw43_initialized = false;
};
