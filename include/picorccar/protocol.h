#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol
{
inline constexpr std::uint8_t SESSION_HELLO_PACKET_COUNT = 3;
inline constexpr std::size_t CTRL_STATE_WIRE_SIZE = sizeof(std::uint8_t) + (2 * sizeof(std::uint16_t));

struct CtrlState
{
    bool m_bpressed{false};
    std::uint16_t m_x_axis{0};
    std::uint16_t m_y_axis{0};
};

struct RCCarPacket
{
    enum class Mode : std::uint8_t
    {
        COMMAND = 0,
        HELLO = 1,
        LAST
    };

    Mode          m_mode {Mode::LAST};
    std::uint32_t m_session_id {0};
    std::uint32_t m_session_ms {0};
    std::uint8_t  m_payload[CTRL_STATE_WIRE_SIZE] {};
};

inline constexpr std::size_t RCCAR_PACKET_PAYLOAD_SIZE = CTRL_STATE_WIRE_SIZE;
inline constexpr std::size_t RCCAR_PACKET_SIZE =
    sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) + RCCAR_PACKET_PAYLOAD_SIZE;

void serialize_ctrl_state(const CtrlState& p_ctrl_state, std::uint8_t* p_destination);

CtrlState deserialize_ctrl_state(const std::uint8_t* p_source);

void serialize_rccar_packet(const RCCarPacket& p_packet, std::uint8_t* p_destination);

RCCarPacket deserialize_rccar_packet(const std::uint8_t* p_source);

} // namespace protocol
