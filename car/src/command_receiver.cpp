#include "command_receiver.h"
#include "picorccar/logger.h"

// pico_sdk
#include "cyw43.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

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

    if (p_packet->tot_len != protocol::COMMAND_PACKET_SIZE)
    {
        LOG_WARNING("Ignoring UDP packet with unexpected length: got=%u expected=%u",
                    static_cast<unsigned>(p_packet->tot_len),
                    static_cast<unsigned>(protocol::COMMAND_PACKET_SIZE));
        pbuf_free(p_packet);
        return;
    }

    protocol::CmdPacket cmd_packet{};
    ReceivedCommand latest_received_command{};
    latest_received_command.received_ms = to_ms_since_boot(get_absolute_time());
    const u16_t copied_bytes = pbuf_copy_partial(p_packet,
                                                 &cmd_packet,
                                                 static_cast<u16_t>(protocol::COMMAND_PACKET_SIZE),
                                                 0);

    pbuf_free(p_packet);

    if (copied_bytes != protocol::COMMAND_PACKET_SIZE)
    {
        LOG_WARNING("Ignoring UDP packet with incomplete copy: got=%u expected=%u",
                    static_cast<unsigned>(copied_bytes),
                    static_cast<unsigned>(protocol::COMMAND_PACKET_SIZE));
        return;
    }

    latest_received_command.sent_ms = cmd_packet.m_sent_us / 1000u;
    latest_received_command.m_ctrl_state = cmd_packet.m_state;

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
