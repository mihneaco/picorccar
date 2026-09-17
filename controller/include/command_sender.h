#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "lwip/ip_addr.h"
#include "picorccar/protocol.h"

struct udp_pcb;

class CommandSender
{
public:
    /** @brief Store AP credentials and remote endpoint; does not init hardware. */
    CommandSender(const char* p_access_point_ssid,
                  const char* p_access_point_password,
                  const char* p_remote_address,
                  std::uint16_t p_remote_port);
    /** @brief Tear down Wi-Fi/UDP if initialized. */
    ~CommandSender();

    CommandSender(const CommandSender& p_other) = delete;
    CommandSender(CommandSender&& p_other) = delete;
    CommandSender& operator=(const CommandSender& p_other) = delete;
    CommandSender& operator=(CommandSender&& p_other) = delete;

    /** @brief Parse the remote address and bring up Wi-Fi, kicking off the STA join. */
    bool init();
    /** @brief Poll join status and open the UDP pcb once the STA link is up. */
    bool connect();
    /** @brief Check whether the UDP pcb exists and the STA link is up. */
    bool is_connected();
    /** @brief Start a new session and send ARM to the car. */
    bool start_new_session();
    /** @brief Send DISARM for the active session, if any. */
    bool end_session();
    /** @brief Send one COM packet with the current joystick state. */
    bool send_controller_state(const protocol::CtrlState& p_ctrl_state);
    /** @brief Send an RST packet requesting the car restart its Wi-Fi stack. */
    bool send_wifi_restart();
    /** @brief Tear down and reinit Wi-Fi, then re-kick the STA join. */
    bool restart_wifi();

private:
    /** @brief Bring up the CYW43 chip and enable STA mode. */
    bool init_wifi();
    /** @brief Build and send an ARM/DISARM packet for the current session. */
    bool send_session_control(protocol::RCCarPacket::ArmFlag p_arm_flag);
    /** @brief Send a one-shot control packet multiple times to cover packet loss. */
    bool send_packet_repeated(protocol::RCCarPacket& p_packet);
    /** @brief Serialize a packet to wire format and send it. */
    bool send_packet(const protocol::RCCarPacket& p_packet);
    /** @brief Send raw bytes over the connected UDP pcb. */
    bool send_packet_bytes(const void* p_payload, std::size_t p_length);
    /** @brief Release the UDP pcb and CYW43 resources. */
    void cleanup();

    // Connection params
    const char* m_access_point_ssid{};
    const char* m_access_point_password{};
    const char* m_remote_address_string{};
    ip_addr_t m_remote_address{};
    std::uint16_t m_remote_port{};

    // Connection state params
    udp_pcb* m_udp_pcb{};
    std::uint32_t m_session_id{};

    bool m_initialized{};
    bool m_wifi_initialized{};
    bool m_session_active{};

    std::uint32_t m_last_conn_attempt_ms{};
};
