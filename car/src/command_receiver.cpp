#include "command_receiver.h"

#include "picorccar/logger.h"
#include "picorccar/pico_common.h"

#include <cstring>

// pico_sdk
#include "cyw43.h"
#include "lwip/def.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace
{
constexpr std::size_t MAC_ADDRESS_LEN = 6;
/// Only one controller is expected; a small margin covers stray associations.
constexpr int MAX_RSSI_CLIENTS = 4;
}

CommandReceiver::CommandReceiver(const char* const p_access_point_ssid,
                                 const char* const p_access_point_password,
                                 const std::uint16_t p_port)
    : m_access_point_ssid(p_access_point_ssid),
      m_access_point_password(p_access_point_password),
      m_server_port(p_port)
{
    critical_section_init(&m_packet_lock);
}

CommandReceiver::~CommandReceiver()
{
    cleanup();
    critical_section_deinit(&m_packet_lock);
}

bool CommandReceiver::init()
{
    if (m_initialized)
    {
        LOG_WARNING("Command receiver already initialized");
        return true;
    }

    if (!init_wifi())
        return false;

    if (!init_server())
        return false;

    m_initialized = true;
    LOG_INFO("Command receiver listening on UDP port %u", static_cast<unsigned>(m_server_port));
    return true;
}

bool CommandReceiver::init_wifi()
{
    // Init CYW43
    LOG_INFO("initializing CYW43");
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43 init failed: %d", cyw43_init_result);
        return false;
    }
    pico_common::enable_led();

#ifdef PICORCCAR_DEBUG
    // Trace every join/auth/deauth/link event the cyw43 sees
    cyw43_state.trace_flags |= CYW43_TRACE_ASYNC_EV;
#endif

    // Enable AP mode
    LOG_INFO("enabling AP mode");
    cyw43_arch_enable_ap_mode(m_access_point_ssid,
                              m_access_point_password,
                              CYW43_AUTH_WPA2_AES_PSK);

    /*
     * AP bring-up applies the chip-wide default PM2 power-save mode, same as STA mode.
     * Keep the radio awake so link dropouts can't originate from a dozing AP radio and
     * both ends are in a known PM state for range testing.
     */
    const int pm_result = cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    if (pm_result != 0)
        LOG_WARNING("cyw43_wifi_pm(CYW43_NONE_PM) failed: %d", pm_result);

    m_wifi_initialized = true;
    return true;
}

bool CommandReceiver::restart_wifi()
{
    LOG_WARNING("Restarting Wi-Fi stack");

    m_rssi_read_failed = false;

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

    if (m_wifi_initialized)
    {
        pico_common::disable_led();
        cyw43_arch_disable_ap_mode();
        cyw43_arch_deinit();
        m_wifi_initialized = false;
    }

    return init_wifi() && init_server();
}

bool CommandReceiver::init_server()
{
    cyw43_arch_lwip_begin();
    {
        m_udp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
        if (m_udp_pcb == nullptr)
        {
            LOG_CRITICAL("udp_new_ip_type failed");

            cyw43_arch_lwip_end();
            cleanup();
            return false;
        }

        const err_t bind_result = udp_bind(m_udp_pcb, IP_ANY_TYPE, m_server_port);
        if (bind_result != ERR_OK)
        {
            LOG_CRITICAL("udp_bind failed: %d", static_cast<int>(bind_result));

            udp_remove(m_udp_pcb);
            m_udp_pcb = nullptr;
            cyw43_arch_lwip_end();
            cleanup();

            return false;
        }

        const udp_recv_fn receive_cbk = [] (void* p_arg,
                                            udp_pcb* p_pcb,
                                            pbuf* p_packet,
                                            const ip_addr_t* p_remote_address,
                                            u16_t p_remote_port)
                                           {
                                                (void)p_pcb;
                                                (void)p_remote_address;
                                                (void)p_remote_port;
                                                auto* const this_ref = static_cast<CommandReceiver*>(p_arg);
                                                if (this_ref != nullptr)
                                                    this_ref->receive_callback(p_packet);
                                                else if (p_packet != nullptr)
                                                    pbuf_free(p_packet);
                                           };
        udp_recv(m_udp_pcb, receive_cbk, this);
    }
    cyw43_arch_lwip_end();

    return true;
}

void CommandReceiver::receive_callback(pbuf* p_packet)
{
    if (p_packet == nullptr)
        return;

    if (p_packet->tot_len != protocol::RCCAR_PACKET_SIZE)
    {
        LOG_WARNING("Ignoring UDP packet with unexpected length: got=%u expected=%u",
                    static_cast<unsigned>(p_packet->tot_len),
                    static_cast<unsigned>(protocol::RCCAR_PACKET_SIZE));
        pbuf_free(p_packet);
        return;
    }

    std::uint8_t payload[protocol::RCCAR_PACKET_SIZE] {};
    const u16_t copied_bytes = pbuf_copy_partial(p_packet,
                                                 payload,
                                                 static_cast<u16_t>(protocol::RCCAR_PACKET_SIZE),
                                                 0);

    pbuf_free(p_packet);

    if (copied_bytes != protocol::RCCAR_PACKET_SIZE)
    {
        LOG_WARNING("Ignoring UDP packet with incomplete copy: got=%u expected=%u",
                    static_cast<unsigned>(copied_bytes),
                    static_cast<unsigned>(protocol::RCCAR_PACKET_SIZE));
        return;
    }

    const auto mode = static_cast<protocol::RCCarPacket::Mode>(payload[protocol::RCCAR_PACKET_MODE_OFFSET]);
    std::uint32_t session_id_be{};
    std::memcpy(&session_id_be, &payload[protocol::RCCAR_PACKET_SESSION_ID_OFFSET], sizeof(session_id_be));
    const std::uint32_t session_id = lwip_ntohl(session_id_be);

    switch (mode)
    {
    case protocol::RCCarPacket::Mode::ARM:
        handle_arm_packet(payload, session_id);
        break;

    case protocol::RCCarPacket::Mode::COM:
        handle_com_packet(payload, session_id);
        break;

    case protocol::RCCarPacket::Mode::RST:
        handle_rst_packet(session_id);
        break;

    default:
        break;
    }
}

void CommandReceiver::handle_arm_packet(const std::uint8_t* p_payload, const std::uint32_t p_session_id)
{
    const auto arm_flag = static_cast<protocol::RCCarPacket::ArmFlag>(
        p_payload[protocol::RCCAR_PACKET_PAYLOAD_OFFSET + protocol::ARM_FLAG_OFFSET]);

    critical_section_enter_blocking(&m_packet_lock);
    {
        if (arm_flag == protocol::RCCarPacket::ArmFlag::Arm)
        {
            m_active_session_id = p_session_id;
            m_session_armed = true;
            m_has_packet = false;
        }
        // Only the controller that owns the active session may tear it down, so a stale
        // disarm from a previous session cannot drop a newer one. Motors are left to the
        // main-loop command-timeout failsafe (the controller stops streaming after disarm).
        else if (arm_flag == protocol::RCCarPacket::ArmFlag::Disarm
                 && p_session_id == m_active_session_id)
        {
            m_active_session_id = 0;
            m_session_armed = false;
            m_has_packet = false;
        }
    }
    critical_section_exit(&m_packet_lock);
}

void CommandReceiver::handle_com_packet(const std::uint8_t* p_payload, const std::uint32_t p_session_id)
{
    critical_section_enter_blocking(&m_packet_lock);
    const bool accept_command = m_session_armed && p_session_id == m_active_session_id;
    critical_section_exit(&m_packet_lock);
    if (!accept_command)
        return;

    std::uint32_t session_ms_be{};
    std::memcpy(&session_ms_be, &p_payload[protocol::RCCAR_PACKET_SESSION_MS_OFFSET], sizeof(session_ms_be));

    std::uint16_t x_axis_be{};
    std::memcpy(&x_axis_be, &p_payload[protocol::RCCAR_PACKET_PAYLOAD_OFFSET + protocol::CTRL_STATE_X_AXIS_OFFSET], sizeof(x_axis_be));

    std::uint16_t y_axis_be{};
    std::memcpy(&y_axis_be, &p_payload[protocol::RCCAR_PACKET_PAYLOAD_OFFSET + protocol::CTRL_STATE_Y_AXIS_OFFSET], sizeof(y_axis_be));

    ReceivedCommand latest_received_command{};
    latest_received_command.m_received_ms = to_ms_since_boot(get_absolute_time());
    latest_received_command.m_sent_ms = lwip_ntohl(session_ms_be);
    latest_received_command.m_ctrl_state.m_x_axis = lwip_ntohs(x_axis_be);
    latest_received_command.m_ctrl_state.m_y_axis = lwip_ntohs(y_axis_be);

    critical_section_enter_blocking(&m_packet_lock);
    {
        m_received_command = latest_received_command;
        m_has_packet = true;
    }
    critical_section_exit(&m_packet_lock);
}

void CommandReceiver::handle_rst_packet(const std::uint32_t p_session_id)
{
    critical_section_enter_blocking(&m_packet_lock);
    {
        if (m_session_armed && p_session_id == m_active_session_id)
            m_restart_requested = true;
    }
    critical_section_exit(&m_packet_lock);
}

bool CommandReceiver::consume_restart_request()
{
    bool restart_requested = false;

    critical_section_enter_blocking(&m_packet_lock);
    {
        restart_requested = m_restart_requested;
        m_restart_requested = false;
    }
    critical_section_exit(&m_packet_lock);

    return restart_requested;
}

std::optional<std::int32_t> CommandReceiver::read_client_rssi()
{
    if (m_rssi_read_failed || !m_wifi_initialized)
        return std::nullopt;

    // num_stas is in/out: buffer capacity in, associated station count out.
    int num_stas = MAX_RSSI_CLIENTS;
    std::uint8_t sta_macs[MAX_RSSI_CLIENTS * MAC_ADDRESS_LEN]{};
    cyw43_wifi_ap_get_stas(&cyw43_state, &num_stas, sta_macs);
    if (num_stas <= 0)
        return std::nullopt;

    LOG_DEBUG("stas=%d mac=%02x:%02x:%02x:%02x:%02x:%02x",
              num_stas,
              sta_macs[0], sta_macs[1], sta_macs[2], sta_macs[3], sta_macs[4], sta_macs[5]);

    /*
     * Per-client RSSI is not wrapped by the driver: WLC_GET_RSSI on the AP interface takes
     * an scb_val_t (32-bit value slot, client MAC, 2 bytes struct padding) and overwrites
     * the value slot with the RSSI in dBm.
     */
    constexpr std::size_t SCB_VAL_SIZE = sizeof(std::int32_t) + MAC_ADDRESS_LEN + 2;
    std::uint8_t scb_val[SCB_VAL_SIZE]{};
    std::memcpy(&scb_val[sizeof(std::int32_t)], sta_macs, MAC_ADDRESS_LEN);
    const int ioctl_result =
        cyw43_ioctl(&cyw43_state, CYW43_IOCTL_GET_RSSI, sizeof(scb_val), scb_val, CYW43_ITF_AP);
    if (ioctl_result != 0)
    {
        /*
         * A timed-out ioctl blocks for the full driver timeout while holding the CYW43 lock,
         * and its late response can desync the SDPCM control channel for every ioctl after
         * it. Latch polling off instead of re-poking a wedged driver every period; the
         * Wi-Fi restart path re-enables it.
         */
        m_rssi_read_failed = true;
        LOG_WARNING("GET_RSSI ioctl failed (%d); polling disabled until Wi-Fi restart", ioctl_result);
        return std::nullopt;
    }

    std::int32_t rssi{};
    std::memcpy(&rssi, scb_val, sizeof(rssi));
    return rssi;
}

bool CommandReceiver::get_packet(ReceivedCommand& p_received_command)
{
    bool has_packet = false;

    critical_section_enter_blocking(&m_packet_lock);
    {
        has_packet = m_has_packet;
        if (has_packet)
        {
            p_received_command = m_received_command;
            m_has_packet = false;
        }
    }
    critical_section_exit(&m_packet_lock);

    return has_packet;
}

void CommandReceiver::cleanup()
{
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

    if (m_wifi_initialized)
    {
        pico_common::disable_led();
        cyw43_arch_disable_ap_mode();
        cyw43_arch_deinit();
        m_wifi_initialized = false;
    }

    m_initialized = false;
}
