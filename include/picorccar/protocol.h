#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol
{
/*
    The controller streams the latest joystick state at least every m_command_interval_ms (sooner on change).
    The car stops and de-arms the session if no valid command arrives within m_command_timeout_ms.
    The timeout must span several intervals so normal UDP loss does not trip a false failsafe.
*/
struct Timing
{
    std::uint32_t m_command_interval_ms; // controller steady-state send floor
    std::uint32_t m_command_timeout_ms;  // car failsafe (runaway) window
};

// Profiles trade runaway-stop latency against tolerance to consecutive packet loss.
inline constexpr Timing TimingBalanced { 50, 250};
inline constexpr Timing TimingRelaxed  {100, 400};
inline constexpr Timing TimingSnappy   { 20, 150};
inline constexpr Timing ACTIVE_TIMING = TimingBalanced;

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
        COM = 0,
        ARM = 1,
        LAST
    };

    Mode          m_mode {Mode::LAST};
    std::uint32_t m_session_id {};
    std::uint32_t m_session_ms {};
    std::uint8_t  m_payload[CTRL_STATE_WIRE_SIZE] {};
};

inline constexpr std::size_t CTRL_STATE_X_AXIS_OFFSET = 0;
inline constexpr std::size_t CTRL_STATE_Y_AXIS_OFFSET = CTRL_STATE_X_AXIS_OFFSET + sizeof(std::uint16_t);

inline constexpr std::size_t RCCAR_PACKET_MODE_OFFSET = 0;
inline constexpr std::size_t RCCAR_PACKET_SESSION_ID_OFFSET = RCCAR_PACKET_MODE_OFFSET + sizeof(std::uint8_t);
inline constexpr std::size_t RCCAR_PACKET_SESSION_MS_OFFSET = RCCAR_PACKET_SESSION_ID_OFFSET + sizeof(std::uint32_t);
inline constexpr std::size_t RCCAR_PACKET_PAYLOAD_OFFSET = RCCAR_PACKET_SESSION_MS_OFFSET + sizeof(std::uint32_t);
inline constexpr std::size_t RCCAR_PACKET_PAYLOAD_SIZE = CTRL_STATE_WIRE_SIZE;
inline constexpr std::size_t RCCAR_PACKET_SIZE =
    sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) + RCCAR_PACKET_PAYLOAD_SIZE;

} // namespace protocol
