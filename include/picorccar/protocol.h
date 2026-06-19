#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol
{
inline constexpr std::uint32_t KEEP_ALIVE_MS = 5000;

struct CtrlState
{
    // Joystick ADC readings can jitter a few counts even when the stick is steady.
    static constexpr std::uint16_t ADC_READ_TOLERANCE = 5;

    std::uint16_t m_x_axis {};
    std::uint16_t m_y_axis {};

    bool is_approx_eq(const CtrlState& p_other) const
    {
        const std::uint16_t x_axis_delta =
            m_x_axis > p_other.m_x_axis ? m_x_axis - p_other.m_x_axis : p_other.m_x_axis - m_x_axis;
        const std::uint16_t y_axis_delta =
            m_y_axis > p_other.m_y_axis ? m_y_axis - p_other.m_y_axis : p_other.m_y_axis - m_y_axis;

        return x_axis_delta < ADC_READ_TOLERANCE && y_axis_delta < ADC_READ_TOLERANCE;
    }
};
inline constexpr std::size_t CTRL_STATE_WIRE_SIZE = 2 * sizeof(std::uint16_t);

struct RCCarPacket
{
    enum class Mode : std::uint8_t
    {
        COMMAND = 0,
        HELLO   = 1,
        LAST
    };

    Mode          m_mode {Mode::LAST};
    std::uint32_t m_session_id {};
    std::uint32_t m_session_ms {};
    std::uint8_t  m_payload[CTRL_STATE_WIRE_SIZE] {};
};

inline constexpr std::size_t RCCAR_PACKET_PAYLOAD_SIZE = CTRL_STATE_WIRE_SIZE;
inline constexpr std::size_t RCCAR_PACKET_SIZE =
    sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) + RCCAR_PACKET_PAYLOAD_SIZE;

} // namespace protocol
