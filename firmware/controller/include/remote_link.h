#pragma once

#include <cstddef>
#include <cstdint>

#include "ble/gatt_client.h"
#include "pico/critical_section.h"

class RemoteLink
{
public:
    static constexpr std::size_t MAX_PACKET_BYTES = 20;

    enum class State : std::uint8_t
    {
        Uninitialized,
        Scanning,
        Connecting,
        DiscoveringService,
        DiscoveringCommand,
        Ready,
        Error
    };

    explicit RemoteLink(const char* p_peer_name);
    ~RemoteLink();

    RemoteLink(const RemoteLink& p_other) = delete;
    RemoteLink(RemoteLink&& p_other) = delete;
    RemoteLink& operator=(const RemoteLink& p_other) = delete;
    RemoteLink& operator=(RemoteLink&& p_other) = delete;

    bool init();
    bool send_packet(const std::uint8_t* p_payload, std::size_t p_length);
    State state() const;
    bool is_ready() const;

private:
    static constexpr std::uint16_t INVALID_CONNECTION_HANDLE = 0xffff;
    static constexpr std::uint16_t INVALID_ATTRIBUTE_HANDLE = 0x0000;

    static void hci_packet_handler(std::uint8_t p_packet_type,
                                   std::uint16_t p_channel,
                                   std::uint8_t* p_packet,
                                   std::uint16_t p_size);
    static void gatt_packet_handler(std::uint8_t p_packet_type,
                                    std::uint16_t p_channel,
                                    std::uint8_t* p_packet,
                                    std::uint16_t p_size);
    static void request_write_callback(void* p_context);
    static void write_ready_callback(void* p_context);

    // BTstack packet callbacks do not provide user context.
    static RemoteLink* m_callback_remote_link;

    void start_scan();
    void handle_advertising_report(const std::uint8_t* p_advertising_data,
                                   std::uint8_t p_advertising_data_length,
                                   const std::uint8_t* p_address,
                                   std::uint8_t p_address_type);
    bool advertising_data_matches_peer(const std::uint8_t* p_advertising_data,
                                       std::uint8_t p_advertising_data_length) const;
    void handle_connection_complete(std::uint8_t p_status, std::uint16_t p_connection_handle);
    void handle_disconnected(std::uint16_t p_connection_handle);
    void handle_gatt_event(std::uint8_t* p_packet);
    void request_write();
    void handle_write_ready();
    void set_state(State p_state);
    void cleanup();

    const char* m_peer_name;
    mutable critical_section_t m_lock{};
    btstack_packet_callback_registration_t m_hci_event_callback_registration{};
    btstack_context_callback_registration_t m_request_write_registration{};
    btstack_context_callback_registration_t m_write_ready_registration{};
    gatt_client_service_t m_remote_service{};
    gatt_client_characteristic_t m_command_characteristic{};
    std::uint16_t m_connection_handle = INVALID_CONNECTION_HANDLE;
    std::uint16_t m_command_value_handle = INVALID_ATTRIBUTE_HANDLE;
    std::uint8_t m_pending_payload[MAX_PACKET_BYTES]{};
    std::uint16_t m_pending_length = 0;
    State m_state = State::Uninitialized;
    bool m_service_found = false;
    bool m_command_found = false;
    bool m_has_pending_packet = false;
    bool m_write_request_pending = false;
    bool m_initialized = false;
    bool m_cyw43_initialized = false;
};
