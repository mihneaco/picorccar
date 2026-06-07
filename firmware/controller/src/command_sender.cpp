#include "command_sender.h"

#include "pico_logger.h"

#include "cyw43.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;

ip4_addr_t make_ipv4_address(const std::uint32_t p_address)
{
    ip4_addr_t address{};
    address.addr = PP_HTONL(p_address);
    return address;
}
}

CommandSender::CommandSender(const char* const p_access_point_ssid,
                             const char* const p_access_point_password,
                             const ip_addr_t& p_remote_address,
                             const std::uint16_t p_remote_port)
    : m_access_point_ssid(p_access_point_ssid),
      m_access_point_password(p_access_point_password),
      m_remote_address(p_remote_address),
      m_remote_port(p_remote_port)
{
}

CommandSender::~CommandSender()
{
    cleanup();
}

bool CommandSender::init()
{
    if (m_initialized)
    {
        LOG_WARNING("Command sender already initialized");
        return true;
    }

    if (!IP_IS_V4_VAL(m_remote_address))
    {
        LOG_CRITICAL("Command sender currently supports only IPv4 destinations");
        return false;
    }

    LOG_INFO("initializing CYW43");
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43 init failed: %d", cyw43_init_result);
        return false;

        LOG_INFO("initializing CYW43");
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    m_cyw43_initialized = true;

    LOG_INFO("enabling STA mode");
    cyw43_arch_enable_sta_mode();

    if (!configure_station_ip())
    {
        cleanup();
        return false;
    }

    LOG_INFO("connecting to AP ssid=%s", m_access_point_ssid);
    const int connect_result = cyw43_arch_wifi_connect_timeout_ms(m_access_point_ssid,
                                                                  m_access_point_password,
                                                                  CYW43_AUTH_WPA2_AES_PSK,
                                                                  WIFI_CONNECT_TIMEOUT_MS);
    if (connect_result != PICO_OK)
    {
        LOG_CRITICAL("Wi-Fi connect failed: %d", connect_result);
        cleanup();
        return false;
    }

    char remote_address[IPADDR_STRLEN_MAX]{};
    ipaddr_ntoa_r(&m_remote_address, remote_address, sizeof(remote_address));

    cyw43_arch_lwip_begin();
    {
        m_udp_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
        if (m_udp_pcb == nullptr)
        {
            cyw43_arch_lwip_end();
            LOG_CRITICAL("udp_new_ip_type failed");
            cleanup();
            return false;
        }

        const err_t connect_result = udp_connect(m_udp_pcb, &m_remote_address, m_remote_port);
        if (connect_result != ERR_OK)
        {
            udp_remove(m_udp_pcb);
            m_udp_pcb = nullptr;
            cyw43_arch_lwip_end();
            LOG_CRITICAL("udp_connect failed: %d", static_cast<int>(connect_result));
            cleanup();
            return false;
        }
    }
    cyw43_arch_lwip_end();

    m_initialized = true;
    LOG_INFO("Command sender ready for UDP %s:%u", remote_address, static_cast<unsigned>(m_remote_port));
    return true;
}

bool CommandSender::send_packet(const std::uint8_t* const p_payload, const std::size_t p_length)
{
    if (!m_initialized || m_udp_pcb == nullptr)
    {
        LOG_WARNING("Command sender send requested before initialization");
        return false;
    }

    if (p_length == 0)
    {
        LOG_WARNING("Refusing to send empty UDP packet");
        return false;
    }

    if (p_length > MAX_PACKET_BYTES)
    {
        LOG_WARNING("UDP payload too large: %u > %u",
                    static_cast<unsigned>(p_length),
                    static_cast<unsigned>(MAX_PACKET_BYTES));
        return false;
    }

    if (p_payload == nullptr)
    {
        LOG_WARNING("Refusing to send null UDP payload");
        return false;
    }

    err_t send_result = ERR_OK;

    cyw43_arch_lwip_begin();
    {
        pbuf* const packet_buffer = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(p_length), PBUF_RAM);
        if (packet_buffer == nullptr)
        {
            send_result = ERR_MEM;
        }
        else
        {
            const err_t copy_result = pbuf_take(packet_buffer, p_payload, p_length);
            if (copy_result == ERR_OK)
                send_result = udp_send(m_udp_pcb, packet_buffer);
            else
                send_result = copy_result;

            pbuf_free(packet_buffer);
        }
    }
    cyw43_arch_lwip_end();

    if (send_result != ERR_OK)
    {
        LOG_WARNING("udp_send failed: %d", static_cast<int>(send_result));
        return false;
    }

    return true;
}

bool CommandSender::configure_station_ip()
{
    const ip4_addr_t station_address = make_ipv4_address(CYW43_DEFAULT_IP_STA_ADDRESS);
    const ip4_addr_t station_netmask = make_ipv4_address(CYW43_DEFAULT_IP_MASK);
    const ip4_addr_t station_gateway = make_ipv4_address(CYW43_DEFAULT_IP_STA_GATEWAY);

    char station_address_str[IP4ADDR_STRLEN_MAX]{};
    char station_netmask_str[IP4ADDR_STRLEN_MAX]{};
    char station_gateway_str[IP4ADDR_STRLEN_MAX]{};
    ip4addr_ntoa_r(&station_address, station_address_str, sizeof(station_address_str));
    ip4addr_ntoa_r(&station_netmask, station_netmask_str, sizeof(station_netmask_str));
    ip4addr_ntoa_r(&station_gateway, station_gateway_str, sizeof(station_gateway_str));

    cyw43_arch_lwip_begin();
    {
#if LWIP_DHCP
        // The car AP intentionally runs without DHCP, so the controller must
        // drop the default DHCP client and pin a known-good static address.
        dhcp_release_and_stop(&cyw43_state.netif[CYW43_ITF_STA]);
#endif
        netif_set_addr(&cyw43_state.netif[CYW43_ITF_STA],
                       &station_address,
                       &station_netmask,
                       &station_gateway);
    }
    cyw43_arch_lwip_end();

    LOG_INFO("configured STA IP=%s mask=%s gateway=%s",
             station_address_str,
             station_netmask_str,
             station_gateway_str);

    return true;
}

void CommandSender::cleanup()
{
    if (!m_initialized && m_udp_pcb == nullptr && !m_cyw43_initialized)
        return;

    if (m_udp_pcb != nullptr)
    {
        cyw43_arch_lwip_begin();
        {
            udp_disconnect(m_udp_pcb);
            udp_remove(m_udp_pcb);
            m_udp_pcb = nullptr;
        }
        cyw43_arch_lwip_end();
    }

    if (m_cyw43_initialized)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        cyw43_arch_disable_sta_mode();
        cyw43_arch_deinit();
        m_cyw43_initialized = false;
    }

    m_initialized = false;
}
