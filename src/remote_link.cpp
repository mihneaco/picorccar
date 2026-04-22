#include "remote_link.h"

#include "pico_logger.h"

#include <algorithm>

#include "cyw43.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t AP_AUTH_FOR_PASSWORD = CYW43_AUTH_WPA2_AES_PSK;
}

RemoteLink::RemoteLink(const char* const p_access_point_ssid,
                       const char* const p_access_point_password,
                       const std::uint16_t p_port)
    : m_access_point_ssid(p_access_point_ssid),
      m_access_point_password(p_access_point_password),
      m_server_port(p_port)
{
    critical_section_init(&m_packet_lock);
}

RemoteLink::~RemoteLink()
{
    cleanup();
    critical_section_deinit(&m_packet_lock);
}

bool RemoteLink::init()
{
    if (m_initialized)
    {
        LOG_WARNING("Remote link already initialized");
        return true;
    }

    // Init CYW43
    LOG_INFO("initializing CYW43");
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43 init failed: %d", cyw43_init_result);
        return false;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    m_cyw43_initialized = true;

    // Enable AP mode
    const char* const access_point_password =
        m_access_point_password != nullptr && m_access_point_password[0] != '\0' ? m_access_point_password : nullptr;
    const char* const password_type = access_point_password != nullptr ? "WPA2-PSK" : "open";
    LOG_INFO("starting AP ssid=%s auth=%s", m_access_point_ssid, password_type);
    cyw43_arch_enable_ap_mode(m_access_point_ssid,
                              access_point_password,
                              access_point_password != nullptr ? AP_AUTH_FOR_PASSWORD : CYW43_AUTH_OPEN);

    // Bind server and set udp_recv cbk
    cyw43_arch_lwip_begin();
    {
        m_udp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
        if (m_udp_pcb == nullptr)
        {
            cyw43_arch_lwip_end();
            LOG_CRITICAL("udp_new_ip_type failed");
            cleanup();
            return false;
        }

        const err_t bind_result = udp_bind(m_udp_pcb, IP_ANY_TYPE, m_server_port);
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
                [](void* const p_arg,
                    udp_pcb* const p_pcb,
                    pbuf* const p_packet,
                    const ip_addr_t* const p_remote_address,
                    const u16_t p_remote_port)
                {
                    auto* const server = static_cast<RemoteLink*>(p_arg);
                    if (server != nullptr)
                        server->receive_callback(p_pcb, p_packet, p_remote_address, p_remote_port);
                    else if (p_packet != nullptr)
                        pbuf_free(p_packet);
                },
                this);
    }
    cyw43_arch_lwip_end();

    m_initialized = true;
    LOG_INFO("Remote link listening on UDP port %u", static_cast<unsigned>(m_server_port));
    return true;
}

void RemoteLink::receive_callback(udp_pcb* const p_pcb,
                                 pbuf* const p_packet,
                                 const ip_addr_t* const p_remote_address,
                                 const std::uint16_t p_remote_port)
{
    (void)p_pcb;

    if (p_packet == nullptr)
        return;

    Packet latest_packet{};
    if (p_remote_address != nullptr)
        latest_packet.remote_address = *p_remote_address;
    latest_packet.remote_port = p_remote_port;
    latest_packet.received_ms = to_ms_since_boot(get_absolute_time());
    latest_packet.total_length = p_packet->tot_len;
    latest_packet.copied_length = static_cast<std::uint16_t>(
        std::min<std::size_t>(p_packet->tot_len, MAX_PACKET_BYTES));
    latest_packet.truncated = latest_packet.copied_length < latest_packet.total_length;

    if (latest_packet.copied_length > 0)
        pbuf_copy_partial(p_packet, latest_packet.payload, latest_packet.copied_length, 0);

    pbuf_free(p_packet);

    critical_section_enter_blocking(&m_packet_lock);
    {
        if (m_has_packet)
            ++m_overwritten_packets;
        latest_packet.overwritten_packets = m_overwritten_packets;
        m_packet = latest_packet;
        m_has_packet = true;
    }
    critical_section_exit(&m_packet_lock);
}

bool RemoteLink::get_packet(Packet& p_packet)
{
    bool has_packet = false;

    critical_section_enter_blocking(&m_packet_lock);
    {
        has_packet = m_has_packet;
        if (has_packet)
        {
            p_packet = m_packet;
            m_has_packet = false;
            m_overwritten_packets = 0;
        }
    }
    critical_section_exit(&m_packet_lock);

    return has_packet;
}

void RemoteLink::cleanup()
{
    if (!m_initialized && m_udp_pcb == nullptr && !m_cyw43_initialized)
        return;

    if (m_udp_pcb != nullptr)
    {
        cyw43_arch_lwip_begin();
        {
            udp_recv(m_udp_pcb, nullptr, nullptr);
            udp_remove(m_udp_pcb);
            m_udp_pcb = nullptr;
        }
        cyw43_arch_lwip_end();
    }

    if (m_cyw43_initialized)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        cyw43_arch_disable_ap_mode();
        cyw43_arch_deinit();
        m_cyw43_initialized = false;
    }

    m_initialized = false;
}
