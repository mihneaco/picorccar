#include "udp_server.h"

#include "pico_logger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "cyw43.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t AP_AUTH_FOR_PASSWORD = CYW43_AUTH_WPA2_AES_PSK;

const char* auth_name(const bool has_password)
{
    return has_password ? "WPA2-PSK" : "open";
}
}

UDPServer::UDPServer(const char* const access_point_ssid,
                     const char* const access_point_password,
                     const char* const server_ip,
                     const std::uint16_t port)
    : m_access_point_ssid(access_point_ssid),
      m_access_point_password(access_point_password),
      m_server_ip(server_ip),
      m_port(port)
{
    critical_section_init(&m_packet_lock);
}

UDPServer::~UDPServer()
{
    cleanup();
    critical_section_deinit(&m_packet_lock);
}

bool UDPServer::init()
{
    ip_addr_t server_address{};
    if (m_server_ip == nullptr || !ipaddr_aton(m_server_ip, &server_address))
    {
        LOG_CRITICAL("invalid UDP server IP: %s", m_server_ip != nullptr ? m_server_ip : "(null)");
        return false;
    }

    LOG_INFO("initializing CYW43");
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43 init failed: %d", cyw43_init_result);
        return false;
    }
    m_cyw43_initialized = true;

    const bool has_password = m_access_point_password != nullptr && m_access_point_password[0] != '\0';
    LOG_INFO("starting AP ssid=%s auth=%s", m_access_point_ssid, auth_name(has_password));
    cyw43_arch_enable_ap_mode(m_access_point_ssid,
                              has_password ? m_access_point_password : nullptr,
                              has_password ? AP_AUTH_FOR_PASSWORD : CYW43_AUTH_OPEN);

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);

    cyw43_arch_lwip_begin();

    m_udp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (m_udp_pcb == nullptr)
    {
        cyw43_arch_lwip_end();
        LOG_CRITICAL("udp_new_ip_type failed");
        cleanup();
        return false;
    }

    const err_t bind_result = udp_bind(m_udp_pcb, &server_address, m_port);
    if (bind_result != ERR_OK)
    {
        udp_remove(m_udp_pcb);
        m_udp_pcb = nullptr;
        cyw43_arch_lwip_end();
        LOG_CRITICAL("udp_bind failed: %d", static_cast<int>(bind_result));
        cleanup();
        return false;
    }

    udp_recv(m_udp_pcb,
             [](void* const arg,
                udp_pcb* const pcb,
                pbuf* const packet,
                const ip_addr_t* const remote_address,
                const u16_t remote_port)
             {
                 auto* const server = static_cast<UDPServer*>(arg);
                 if (server != nullptr)
                     server->receive_callback(pcb, packet, remote_address, remote_port);
                 else if (packet != nullptr)
                     pbuf_free(packet);
             },
             this);
    cyw43_arch_lwip_end();

    LOG_INFO("UDP server listening on %s:%u", m_server_ip, static_cast<unsigned>(m_port));
    return true;
}

void UDPServer::poll()
{
    Packet packet{};
    if (take_latest_packet(packet))
        print_packet(packet);
}

void UDPServer::receive_callback(udp_pcb* const pcb,
                                 pbuf* const packet,
                                 const ip_addr_t* const remote_address,
                                 const std::uint16_t remote_port)
{
    (void)pcb;

    if (packet == nullptr)
        return;

    Packet latest_packet{};
    if (remote_address != nullptr)
        latest_packet.remote_address = *remote_address;
    latest_packet.remote_port = remote_port;
    latest_packet.received_ms = to_ms_since_boot(get_absolute_time());
    latest_packet.total_length = packet->tot_len;
    latest_packet.copied_length = static_cast<std::uint16_t>(
        std::min<std::size_t>(packet->tot_len, MAX_PACKET_BYTES));
    latest_packet.truncated = latest_packet.copied_length < latest_packet.total_length;

    if (latest_packet.copied_length > 0)
        pbuf_copy_partial(packet, latest_packet.payload, latest_packet.copied_length, 0);

    pbuf_free(packet);

    critical_section_enter_blocking(&m_packet_lock);
    if (m_has_latest_packet)
        ++m_overwritten_packets;
    latest_packet.overwritten_packets = m_overwritten_packets;
    m_latest_packet = latest_packet;
    m_has_latest_packet = true;
    critical_section_exit(&m_packet_lock);
}

bool UDPServer::take_latest_packet(Packet& packet)
{
    critical_section_enter_blocking(&m_packet_lock);
    const bool has_packet = m_has_latest_packet;
    if (has_packet)
    {
        packet = m_latest_packet;
        m_has_latest_packet = false;
        m_overwritten_packets = 0;
    }
    critical_section_exit(&m_packet_lock);

    return has_packet;
}

void UDPServer::print_packet(const Packet& packet)
{
    char remote_address[IPADDR_STRLEN_MAX]{};
    ipaddr_ntoa_r(&packet.remote_address, remote_address, sizeof(remote_address));

    if (packet.truncated)
    {
        LOG_TRACE("UDP packet from %s:%u len=%u printed=%u overwritten=%lu",
                  remote_address,
                  static_cast<unsigned>(packet.remote_port),
                  static_cast<unsigned>(packet.total_length),
                  static_cast<unsigned>(packet.copied_length),
                  static_cast<unsigned long>(packet.overwritten_packets));
    }
    else
    {
        LOG_TRACE("UDP packet from %s:%u len=%u overwritten=%lu",
                  remote_address,
                  static_cast<unsigned>(packet.remote_port),
                  static_cast<unsigned>(packet.total_length),
                  static_cast<unsigned long>(packet.overwritten_packets));
    }

    print_ascii_payload(packet);
    print_hex_payload(packet);
}

void UDPServer::print_ascii_payload(const Packet& packet)
{
    char ascii_payload[MAX_PACKET_BYTES + 4]{};
    std::size_t output_index = 0;
    for (std::uint16_t i = 0; i < packet.copied_length; ++i)
    {
        const unsigned char byte = packet.payload[i];
        ascii_payload[output_index++] = std::isprint(byte) ? static_cast<char>(byte) : '.';
    }
    if (packet.truncated)
    {
        ascii_payload[output_index++] = '.';
        ascii_payload[output_index++] = '.';
        ascii_payload[output_index++] = '.';
    }

    LOG_TRACE("ascii: %s", ascii_payload);
}

void UDPServer::print_hex_payload(const Packet& packet)
{
    constexpr std::size_t MAX_HEX_PAYLOAD_CHARS = (MAX_PACKET_BYTES * 3) + 4;
    static_assert(MAX_HEX_PAYLOAD_CHARS + 40 < ULOG_MAX_MESSAGE_LENGTH,
                  "ULOG_MAX_MESSAGE_LENGTH must fit one UDP packet hex dump line");

    char hex_payload[MAX_HEX_PAYLOAD_CHARS + 1]{};
    std::size_t output_index = 0;

    for (std::uint16_t i = 0; i < packet.copied_length; ++i)
    {
        const int written = std::snprintf(&hex_payload[output_index],
                                          sizeof(hex_payload) - output_index,
                                          "%s%02x",
                                          i == 0 ? "" : " ",
                                          packet.payload[i]);
        if (written < 0)
            return;

        output_index += static_cast<std::size_t>(written);
        if (output_index >= sizeof(hex_payload))
            return;
    }

    if (packet.truncated)
    {
        const int written = std::snprintf(&hex_payload[output_index],
                                          sizeof(hex_payload) - output_index,
                                          " ...");
        if (written < 0)
            return;
    }

    LOG_TRACE("hex: %s", hex_payload);
}

void UDPServer::cleanup()
{
    if (m_udp_pcb != nullptr)
    {
        cyw43_arch_lwip_begin();
        udp_recv(m_udp_pcb, nullptr, nullptr);
        udp_remove(m_udp_pcb);
        m_udp_pcb = nullptr;
        cyw43_arch_lwip_end();
    }

    if (m_cyw43_initialized)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        cyw43_arch_disable_ap_mode();
        cyw43_arch_deinit();
        m_cyw43_initialized = false;
    }
}
