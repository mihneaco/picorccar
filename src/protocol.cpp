#include "picorccar/protocol.h"

#include <cstring>

namespace protocol
{
namespace
{
constexpr std::size_t CTRL_STATE_BUTTON_OFFSET = 0;
constexpr std::size_t CTRL_STATE_X_AXIS_OFFSET = CTRL_STATE_BUTTON_OFFSET + sizeof(std::uint8_t);
constexpr std::size_t CTRL_STATE_Y_AXIS_OFFSET = CTRL_STATE_X_AXIS_OFFSET + sizeof(std::uint16_t);

constexpr std::size_t RCCAR_PACKET_MODE_OFFSET = 0;
constexpr std::size_t RCCAR_PACKET_SESSION_ID_OFFSET = RCCAR_PACKET_MODE_OFFSET + sizeof(std::uint8_t);
constexpr std::size_t RCCAR_PACKET_SESSION_MS_OFFSET = RCCAR_PACKET_SESSION_ID_OFFSET + sizeof(std::uint32_t);
constexpr std::size_t RCCAR_PACKET_PAYLOAD_OFFSET = RCCAR_PACKET_SESSION_MS_OFFSET + sizeof(std::uint32_t);

void write_u16_be(std::uint8_t* const p_destination, const std::uint16_t p_value)
{
    p_destination[0] = static_cast<std::uint8_t>(p_value >> 8);
    p_destination[1] = static_cast<std::uint8_t>(p_value);
}

std::uint16_t read_u16_be(const std::uint8_t* const p_source)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p_source[0]) << 8) |
                                      static_cast<std::uint16_t>(p_source[1]));
}

void write_u32_be(std::uint8_t* const p_destination, const std::uint32_t p_value)
{
    p_destination[0] = static_cast<std::uint8_t>(p_value >> 24);
    p_destination[1] = static_cast<std::uint8_t>(p_value >> 16);
    p_destination[2] = static_cast<std::uint8_t>(p_value >> 8);
    p_destination[3] = static_cast<std::uint8_t>(p_value);
}

std::uint32_t read_u32_be(const std::uint8_t* const p_source)
{
    return (static_cast<std::uint32_t>(p_source[0]) << 24) |
           (static_cast<std::uint32_t>(p_source[1]) << 16) |
           (static_cast<std::uint32_t>(p_source[2]) << 8) |
           static_cast<std::uint32_t>(p_source[3]);
}
} // namespace

void serialize_ctrl_state(const CtrlState& p_ctrl_state, std::uint8_t* const p_destination)
{
    p_destination[CTRL_STATE_BUTTON_OFFSET] = p_ctrl_state.m_bpressed ? 1U : 0U;
    write_u16_be(&p_destination[CTRL_STATE_X_AXIS_OFFSET], p_ctrl_state.m_x_axis);
    write_u16_be(&p_destination[CTRL_STATE_Y_AXIS_OFFSET], p_ctrl_state.m_y_axis);
}

CtrlState deserialize_ctrl_state(const std::uint8_t* const p_source)
{
    return CtrlState{
        .m_bpressed = p_source[CTRL_STATE_BUTTON_OFFSET] != 0,
        .m_x_axis = read_u16_be(&p_source[CTRL_STATE_X_AXIS_OFFSET]),
        .m_y_axis = read_u16_be(&p_source[CTRL_STATE_Y_AXIS_OFFSET])};
}

void serialize_rccar_packet(const RCCarPacket& p_packet, std::uint8_t* const p_destination)
{
    p_destination[RCCAR_PACKET_MODE_OFFSET] = static_cast<std::uint8_t>(p_packet.m_mode);
    write_u32_be(&p_destination[RCCAR_PACKET_SESSION_ID_OFFSET], p_packet.m_session_id);
    write_u32_be(&p_destination[RCCAR_PACKET_SESSION_MS_OFFSET], p_packet.m_session_ms);
    std::memcpy(&p_destination[RCCAR_PACKET_PAYLOAD_OFFSET], p_packet.m_payload, sizeof(p_packet.m_payload));
}

RCCarPacket deserialize_rccar_packet(const std::uint8_t* const p_source)
{
    RCCarPacket packet{};
    packet.m_mode = static_cast<RCCarPacket::Mode>(p_source[RCCAR_PACKET_MODE_OFFSET]);
    packet.m_session_id = read_u32_be(&p_source[RCCAR_PACKET_SESSION_ID_OFFSET]);
    packet.m_session_ms = read_u32_be(&p_source[RCCAR_PACKET_SESSION_MS_OFFSET]);
    std::memcpy(packet.m_payload, &p_source[RCCAR_PACKET_PAYLOAD_OFFSET], sizeof(packet.m_payload));
    return packet;
}
} // namespace protocol
