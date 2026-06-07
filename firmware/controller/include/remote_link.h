#pragma once

#include <cstddef>
#include <cstdint>

#include "lwip/ip_addr.h"

struct udp_pcb;

class RemoteLink
{
public:
    static constexpr std::size_t MAX_PACKET_BYTES = 256;

    RemoteLink(const char* p_access_point_ssid,
               const char* p_access_point_password,
               const ip_addr_t& p_remote_address,
               std::uint16_t p_remote_port);
    ~RemoteLink();

    RemoteLink(const RemoteLink& p_other) = delete;
    RemoteLink(RemoteLink&& p_other) = delete;
    RemoteLink& operator=(const RemoteLink& p_other) = delete;
    RemoteLink& operator=(RemoteLink&& p_other) = delete;

    bool init();
    bool send_packet(const std::uint8_t* p_payload, std::size_t p_length);

private:
    bool configure_station_ip();
    void cleanup();

    const char* m_access_point_ssid;
    const char* m_access_point_password;
    ip_addr_t m_remote_address{};
    std::uint16_t m_remote_port;

    udp_pcb* m_udp_pcb = nullptr;
    bool m_initialized = false;
    bool m_cyw43_initialized = false;
};
