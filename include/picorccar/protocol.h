#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol
{

struct CtrlState
{
    std::uint16_t m_x_axis{0};
    std::uint16_t m_y_axis{0};
    bool          m_bpressed{false};
};

struct CmdPacket
{
    CtrlState     m_state{};
    std::uint32_t m_sent_us{0};
    std::uint32_t m_session_id{0};
    std::uint32_t m_session_ms{0};
};
inline constexpr std::size_t COMMAND_PACKET_SIZE = sizeof(CmdPacket);

struct HskPacket
{
    enum class Type : std::uint8_t
    {
        CLIENT_HELLO = 0,
        SERVER_HELLO = 1,
        LAST
    };

    Type m_mode{Type::LAST};
    std::uint32_t m_session_id{0};
    std::uint32_t m_boot_ms{0};
};

} // namespace packet
