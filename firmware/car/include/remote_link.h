#pragma once

#include <cstddef>
#include <cstdint>

#include "pico/critical_section.h"

class RemoteLink
{
public:
    static constexpr std::size_t MAX_PACKET_BYTES = 20;

    enum class State : std::uint8_t
    {
        Uninitialized,
        Advertising,
        Connected,
        Error
    };

    struct Packet
    {
        std::uint32_t m_received_ms = 0;
        std::uint32_t m_overwritten_packets = 0;
        std::uint16_t m_total_length = 0;
        std::uint16_t m_copied_length = 0;
        std::uint8_t m_payload[MAX_PACKET_BYTES]{};
        bool m_truncated = false;
    };

    explicit RemoteLink(const char* p_device_name);
    ~RemoteLink();

    RemoteLink(const RemoteLink& p_other) = delete;
    RemoteLink(RemoteLink&& p_other) = delete;
    RemoteLink& operator=(const RemoteLink& p_other) = delete;
    RemoteLink& operator=(RemoteLink&& p_other) = delete;

    bool init();
    bool get_packet(Packet& p_packet);
    State state() const;
    bool is_connected() const;

private:
    static constexpr std::uint16_t INVALID_CONNECTION_HANDLE = 0xffff;
    static constexpr std::uint16_t INVALID_ATTRIBUTE_HANDLE = 0x0000;
    static constexpr std::size_t MAX_ADVERTISING_DATA_BYTES = 31;

    static void hci_packet_handler(std::uint8_t p_packet_type,
                                   std::uint16_t p_channel,
                                   std::uint8_t* p_packet,
                                   std::uint16_t p_size);
    static void att_packet_handler(std::uint8_t p_packet_type,
                                   std::uint16_t p_channel,
                                   std::uint8_t* p_packet,
                                   std::uint16_t p_size);
    static int att_write_callback(std::uint16_t p_connection_handle,
                                  std::uint16_t p_attribute_handle,
                                  std::uint16_t p_transaction_mode,
                                  std::uint16_t p_offset,
                                  std::uint8_t* p_buffer,
                                  std::uint16_t p_buffer_size);

    bool configure_advertising();
    void handle_stack_ready();
    void handle_connected(std::uint16_t p_connection_handle);
    void handle_disconnected(std::uint16_t p_connection_handle);
    void handle_write(std::uint16_t p_attribute_handle,
                      const std::uint8_t* p_buffer,
                      std::uint16_t p_buffer_size);
    void cleanup();

    const char* m_device_name;
    mutable critical_section_t m_lock{};
    Packet m_packet{};
    std::uint32_t m_overwritten_packets = 0;
    // @details Use only with m_lock locked
    bool m_has_packet = false;
    std::uint16_t m_connection_handle = INVALID_CONNECTION_HANDLE;
    std::uint16_t m_command_value_handle = INVALID_ATTRIBUTE_HANDLE;
    State m_state = State::Uninitialized;
    std::uint8_t m_advertising_data[MAX_ADVERTISING_DATA_BYTES]{};
    std::uint8_t m_advertising_data_length = 0;
    bool m_initialized = false;
    bool m_cyw43_initialized = false;
};
