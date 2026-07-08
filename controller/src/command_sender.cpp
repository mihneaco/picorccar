#include "command_sender.h"

#include "picorccar/logger.h"
#include "picorccar/pico_common.h"

#include <cstring>

// pico_sdk
#include "cyw43.h"
#include "lwip/def.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

namespace
{
constexpr std::uint32_t SESSION_ARM_PACKET_SPACING_MS = 20;
constexpr std::uint8_t  SESSION_ARM_PACKET_COUNT      =  3;

constexpr std::uint32_t RECONNECT_ATTEMPT_MS = 1000;
}

CommandSender::CommandSender(const char* const p_access_point_ssid,
                             const char* const p_access_point_password,
                             const char* const p_remote_address,
                             const std::uint16_t p_remote_port)
    : m_access_point_ssid(p_access_point_ssid),
      m_access_point_password(p_access_point_password),
      m_remote_address_string(p_remote_address),
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

    if (!ipaddr_aton(m_remote_address_string, &m_remote_address))
    {
        LOG_CRITICAL("Failed to parse UDP server IP '%s'", m_remote_address_string);
        return false;
    }

    if (!IP_IS_V4_VAL(m_remote_address))
    {
        LOG_CRITICAL("Command sender currently supports only IPv4 destinations");
        return false;
    }

    if (!init_wifi())
        return false;

    // Kick off the join without blocking boot on it: if the car isn't up yet or the first
    // attempt fails, the main loop's connect()/is_connected() cycle will keep retrying.
    LOG_INFO("connecting to AP ssid=%s", m_access_point_ssid);
    const int connect_result = cyw43_arch_wifi_connect_async(m_access_point_ssid,
                                                             m_access_point_password,
                                                             CYW43_AUTH_WPA2_AES_PSK);
    if (connect_result != 0)
        LOG_WARNING("Wi-Fi connect kick-off failed: %d; retrying from main loop", connect_result);

    m_initialized = true;
    LOG_INFO("Command sender ready");
    return true;
}

bool CommandSender::init_wifi()
{
    LOG_INFO("initializing CYW43");
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43 init failed: %d", cyw43_init_result);
        return false;
    }
    pico_common::enable_led();

#ifdef PICORCCAR_DEBUG
    // Trace every join/auth/deauth/link event the driver sees, with its real status/reason
    // code, instead of only the coarse link_status a poll loop would observe.
    cyw43_state.trace_flags |= CYW43_TRACE_ASYNC_EV;
#endif

    LOG_INFO("enabling STA mode");
    cyw43_arch_enable_sta_mode();
    const ip4_addr_t station_address{
        .addr = lwip_htonl(CYW43_DEFAULT_IP_STA_ADDRESS)};
    const ip4_addr_t station_netmask{
        .addr = lwip_htonl(CYW43_DEFAULT_IP_MASK)};
    const ip4_addr_t station_gateway{
        .addr = lwip_htonl(CYW43_DEFAULT_IP_STA_GATEWAY)};
    cyw43_arch_lwip_begin();
    {
        netif_set_addr(&cyw43_state.netif[CYW43_ITF_STA],
                       &station_address,
                       &station_netmask,
                       &station_gateway);
    }
    cyw43_arch_lwip_end();
    m_wifi_initialized = true;

    return true;
}

bool CommandSender::restart_wifi()
{
    LOG_WARNING("Restarting Wi-Fi stack");

    if (m_udp_pcb != nullptr)
    {
        cyw43_arch_lwip_begin();
        {
            udp_remove(m_udp_pcb);
            m_udp_pcb = nullptr;
        }
        cyw43_arch_lwip_end();
    }

    if (m_wifi_initialized)
    {
        pico_common::disable_led();
        cyw43_arch_disable_sta_mode();
        cyw43_arch_deinit();
        m_wifi_initialized = false;
    }

    if (!init_wifi())
        return false;

    const int connect_result = cyw43_arch_wifi_connect_async(m_access_point_ssid,
                                                             m_access_point_password,
                                                             CYW43_AUTH_WPA2_AES_PSK);
    if (connect_result != 0)
        LOG_WARNING("Wi-Fi connect kick-off failed: %d; retrying from main loop", connect_result);

    return true;
}

bool CommandSender::connect()
{
    if (!m_wifi_initialized)
    {
        LOG_ERROR("Wifi not initialized, skiping connect()");
        return false;
    }

    const std::uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - m_last_conn_attempt_ms < RECONNECT_ATTEMPT_MS)
    {
        return false;
    }
    m_last_conn_attempt_ms = now_ms;

    const int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

    if (link_status != CYW43_LINK_UP)
    {
        // CYW43_LINK_JOIN means a join is already in flight - let it finish rather than
        // restarting it every RECONNECT_ATTEMPT_MS.
        if (link_status != CYW43_LINK_JOIN)
        {
            LOG_WARNING("STA link down (status=%d), reconnecting to ssid=%s", link_status, m_access_point_ssid);
            cyw43_arch_wifi_connect_async(m_access_point_ssid, m_access_point_password, CYW43_AUTH_WPA2_AES_PSK);
        }
        return false;
    }

    cyw43_arch_lwip_begin();
    {
        if (m_udp_pcb != nullptr)
        {
            udp_remove(m_udp_pcb);
            m_udp_pcb = nullptr;
        }

        m_udp_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
        if (m_udp_pcb == nullptr)
        {
            cyw43_arch_lwip_end();
            LOG_CRITICAL("udp_new_ip_type failed");
            cleanup();
            return false;
        }

        char remote_address[IPADDR_STRLEN_MAX]{};
        ipaddr_ntoa_r(&m_remote_address, remote_address, sizeof(remote_address));

        const err_t connect_result = udp_connect(m_udp_pcb, &m_remote_address, m_remote_port);
        if (connect_result != ERR_OK)
        {
            LOG_CRITICAL("udp_connect failed: %d", static_cast<int>(connect_result));

            udp_remove(m_udp_pcb);
            m_udp_pcb = nullptr;
            cyw43_arch_lwip_end();
            cleanup();

            return false;
        }
    }
    cyw43_arch_lwip_end();

    return true;
}

bool CommandSender::is_connected()
{
    if (!m_initialized || m_udp_pcb == nullptr)
        return false;

    // udp_connect() has no handshake and can't detect a lost association on its own, so the
    // pcb existing above is not enough - confirm the STA link is actually up.
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

bool CommandSender::start_new_session()
{
    LOG_INFO();

    if (!m_initialized)
    {
        LOG_WARNING("Command sender session start requested before initialization");
        return false;
    }

    m_session_id = get_rand_32();
    m_session_active = true;

    return send_session_control(protocol::RCCarPacket::ArmFlag::Arm);
}

bool CommandSender::end_session()
{
    LOG_INFO();

    if (!m_initialized)
    {
        LOG_WARNING("Command sender session end requested before initialization");
        return false;
    }

    if (!m_session_active)
        return true;

    m_session_active = false;

    return send_session_control(protocol::RCCarPacket::ArmFlag::Disarm);
}

bool CommandSender::send_session_control(const protocol::RCCarPacket::ArmFlag p_arm_flag)
{
    // Repeat the control packet a few times: it is a one-shot state change on a lossy link, so
    // unlike the streamed COM packets there is no next packet to cover a drop.
    protocol::RCCarPacket arm_packet{};
    arm_packet.m_mode = protocol::RCCarPacket::Mode::ARM;
    arm_packet.m_session_id = m_session_id;
    arm_packet.m_payload[protocol::ARM_FLAG_OFFSET] = static_cast<std::uint8_t>(p_arm_flag);

    bool sent_all_packets = true;
    for (std::uint8_t idx = 0; idx < SESSION_ARM_PACKET_COUNT; ++idx)
    {
        arm_packet.m_session_ms = to_ms_since_boot(get_absolute_time());
        sent_all_packets = send_packet(arm_packet) && sent_all_packets;
        sleep_ms(SESSION_ARM_PACKET_SPACING_MS);
    }

    return sent_all_packets;
}

bool CommandSender::send_controller_state(const protocol::CtrlState& p_ctrl_state)
{
    if (!m_session_active)
    {
        LOG_WARNING("Command sender controller state send requested before session start");
        return false;
    }

    if (!is_connected())
    {
        LOG_ERROR("Client is not connected");
        return false;
    }

    protocol::RCCarPacket command_packet{};
    command_packet.m_mode = protocol::RCCarPacket::Mode::COM;
    command_packet.m_session_id = m_session_id;
    command_packet.m_session_ms = to_ms_since_boot(get_absolute_time());

    const std::uint16_t x_axis_be = lwip_htons(p_ctrl_state.m_x_axis);
    std::memcpy(&command_packet.m_payload[protocol::CTRL_STATE_X_AXIS_OFFSET], &x_axis_be, sizeof(x_axis_be));

    const std::uint16_t y_axis_be = lwip_htons(p_ctrl_state.m_y_axis);
    std::memcpy(&command_packet.m_payload[protocol::CTRL_STATE_Y_AXIS_OFFSET], &y_axis_be, sizeof(y_axis_be));

    return send_packet(command_packet);
}

bool CommandSender::send_packet(const protocol::RCCarPacket &p_packet)
{
    std::uint8_t payload[protocol::RCCAR_PACKET_SIZE] {};
    payload[protocol::RCCAR_PACKET_MODE_OFFSET] = static_cast<std::uint8_t>(p_packet.m_mode);

    const std::uint32_t session_id_be = lwip_htonl(p_packet.m_session_id);
    std::memcpy(&payload[protocol::RCCAR_PACKET_SESSION_ID_OFFSET], &session_id_be, sizeof(session_id_be));

    const std::uint32_t session_ms_be = lwip_htonl(p_packet.m_session_ms);
    std::memcpy(&payload[protocol::RCCAR_PACKET_SESSION_MS_OFFSET], &session_ms_be, sizeof(session_ms_be));

    std::memcpy(&payload[protocol::RCCAR_PACKET_PAYLOAD_OFFSET], p_packet.m_payload, sizeof(p_packet.m_payload));

    return send_packet_bytes(payload, sizeof(payload));
}

bool CommandSender::send_packet_bytes(const void* const p_payload, const std::size_t p_length)
{
    if (!is_connected())
    {
        LOG_ERROR("Client is not connected");
        return false;
    }

    if (p_length > protocol::RCCAR_PACKET_SIZE)
    {
        LOG_WARNING("Payload too large: %u > %u",
                    static_cast<unsigned>(p_length),
                    static_cast<unsigned>(protocol::RCCAR_PACKET_SIZE));
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
            const err_t res = pbuf_take(packet_buffer, p_payload, p_length);
            if (res == ERR_OK)
                send_result = udp_send(m_udp_pcb, packet_buffer);
            else
                send_result = res;

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

void CommandSender::cleanup()
{
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

    if (m_wifi_initialized)
    {
        pico_common::disable_led();
        cyw43_arch_disable_sta_mode();
        cyw43_arch_deinit();
        m_wifi_initialized = false;
    }

    m_initialized = false;
    m_session_active = false;
    m_session_id = 0;
}
