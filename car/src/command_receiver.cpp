#include "command_receiver.h"

#include "picorccar/logger.h"

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
constexpr std::size_t RCCAR_PACKET_MODE_OFFSET = 0;
constexpr std::size_t RCCAR_PACKET_SESSION_ID_OFFSET = RCCAR_PACKET_MODE_OFFSET + sizeof(std::uint8_t);
constexpr std::size_t RCCAR_PACKET_SESSION_MS_OFFSET = RCCAR_PACKET_SESSION_ID_OFFSET + sizeof(std::uint32_t);
constexpr std::size_t RCCAR_PACKET_PAYLOAD_OFFSET = RCCAR_PACKET_SESSION_MS_OFFSET + sizeof(std::uint32_t);

constexpr std::size_t CTRL_STATE_X_AXIS_OFFSET = 0;
constexpr std::size_t CTRL_STATE_Y_AXIS_OFFSET = CTRL_STATE_X_AXIS_OFFSET + sizeof(std::uint16_t);
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
    LOG_INFO("enabling AP mode");
    cyw43_arch_enable_ap_mode(m_access_point_ssid,
                              m_access_point_password,
                              CYW43_AUTH_WPA2_AES_PSK);

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

    m_initialized = true;
    LOG_INFO("Command receiver listening on UDP port %u", static_cast<unsigned>(m_server_port));
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

    const auto mode = static_cast<protocol::RCCarPacket::Mode>(payload[RCCAR_PACKET_MODE_OFFSET]);

    std::uint32_t session_id_be{};
    std::memcpy(&session_id_be, &payload[RCCAR_PACKET_SESSION_ID_OFFSET], sizeof(session_id_be));
    const std::uint32_t session_id = lwip_ntohl(session_id_be);

    if (mode == protocol::RCCarPacket::Mode::HELLO)
    {
        critical_section_enter_blocking(&m_packet_lock);
        {
            if (!m_session_armed)
            {
                m_active_session_id = session_id;
                m_session_armed = true;
                m_has_packet = false;
            }
        }
        critical_section_exit(&m_packet_lock);
        return;
    }

    if (mode != protocol::RCCarPacket::Mode::COMMAND)
    {
        LOG_DEBUG("Ignoring non-command packet mode=%u",
                  static_cast<unsigned>(mode));
        return;
    }

    critical_section_enter_blocking(&m_packet_lock);
    const bool accept_command = m_session_armed && session_id == m_active_session_id;
    critical_section_exit(&m_packet_lock);
    if (!accept_command)
        return;

    std::uint32_t session_ms_be{};
    std::memcpy(&session_ms_be, &payload[RCCAR_PACKET_SESSION_MS_OFFSET], sizeof(session_ms_be));

    std::uint16_t x_axis_be{};
    std::memcpy(&x_axis_be, &payload[RCCAR_PACKET_PAYLOAD_OFFSET + CTRL_STATE_X_AXIS_OFFSET], sizeof(x_axis_be));

    std::uint16_t y_axis_be{};
    std::memcpy(&y_axis_be, &payload[RCCAR_PACKET_PAYLOAD_OFFSET + CTRL_STATE_Y_AXIS_OFFSET], sizeof(y_axis_be));

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

void CommandReceiver::reset_session()
{
    critical_section_enter_blocking(&m_packet_lock);
    {
        m_has_packet = false;
        m_active_session_id = 0;
        m_session_armed = false;
    }
    critical_section_exit(&m_packet_lock);
}

void CommandReceiver::cleanup()
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
