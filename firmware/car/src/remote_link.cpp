#include "remote_link.h"

#include "ble/ble_conf.h"
#include "car_gatt.h"
#include "pico_logger.h"

#include <algorithm>
#include <cstring>

#include "btstack.h"
#include "cyw43.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace
{
constexpr std::uint8_t ADVERTISING_FLAGS = 0x06;
constexpr std::uint16_t ADVERTISING_INTERVAL_MIN_UNITS = 0x0030;
constexpr std::uint16_t ADVERTISING_INTERVAL_MAX_UNITS = 0x0030;
constexpr std::uint8_t ADVERTISING_TYPE_CONNECTABLE_UNDIRECTED = 0;
constexpr std::uint8_t ADVERTISING_CHANNEL_MAP_ALL = 0x07;
constexpr std::uint8_t ADVERTISING_FILTER_POLICY_ANY = 0x00;
constexpr std::size_t BLE_UUID_BYTES = 16;

RemoteLink* active_remote_link = nullptr;
btstack_packet_callback_registration_t hci_event_callback_registration{};

bool append_advertising_field(std::uint8_t* const p_advertising_data,
                              const std::size_t p_capacity,
                              std::size_t& p_offset,
                              const std::uint8_t p_type,
                              const std::uint8_t* const p_data,
                              const std::size_t p_data_length)
{
    constexpr std::size_t ADVERTISING_FIELD_HEADER_BYTES = 2;
    constexpr std::size_t ADVERTISING_FIELD_TYPE_BYTES = 1;
    constexpr std::size_t ADVERTISING_FIELD_MAX_VALUE_BYTES = 254;

    if (p_data == nullptr || p_data_length > ADVERTISING_FIELD_MAX_VALUE_BYTES)
        return false;

    const std::size_t field_size = ADVERTISING_FIELD_HEADER_BYTES + p_data_length;
    if (p_offset + field_size > p_capacity)
        return false;

    p_advertising_data[p_offset++] = static_cast<std::uint8_t>(p_data_length + ADVERTISING_FIELD_TYPE_BYTES);
    p_advertising_data[p_offset++] = p_type;
    std::memcpy(&p_advertising_data[p_offset], p_data, p_data_length);
    p_offset += p_data_length;
    return true;
}

void reverse_uuid(const std::uint8_t* const p_uuid, std::uint8_t* const p_reversed_uuid)
{
    for (std::size_t index = 0; index < BLE_UUID_BYTES; ++index)
        p_reversed_uuid[index] = p_uuid[BLE_UUID_BYTES - 1 - index];
}
}

RemoteLink::RemoteLink(const char* const p_device_name)
    : m_device_name(p_device_name)
{
    critical_section_init(&m_lock);
}

RemoteLink::~RemoteLink()
{
    cleanup();
    critical_section_deinit(&m_lock);
}

bool RemoteLink::init()
{
    if (m_initialized)
    {
        LOG_WARNING("Remote link already initialized");
        return true;
    }

    if (active_remote_link != nullptr && active_remote_link != this)
    {
        LOG_CRITICAL("Only one BLE remote link instance is supported");
        return false;
    }

    LOG_INFO("initializing CYW43/BLE");
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43/BLE init failed: %d", cyw43_init_result);
        return false;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    m_cyw43_initialized = true;
    active_remote_link = this;

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    att_server_init(profile_data, nullptr, att_write_callback);
    att_server_register_packet_handler(att_packet_handler);

    std::uint16_t service_start_handle = INVALID_ATTRIBUTE_HANDLE;
    std::uint16_t service_end_handle = INVALID_ATTRIBUTE_HANDLE;
    if (!gatt_server_get_handle_range_for_service_with_uuid128(common::BLE_SERVICE_UUID,
                                                               &service_start_handle,
                                                               &service_end_handle))
    {
        LOG_CRITICAL("BLE command service handle range not found");
        cleanup();
        return false;
    }

    m_command_value_handle = gatt_server_get_value_handle_for_characteristic_with_uuid128(
        service_start_handle,
        service_end_handle,
        common::BLE_COMMAND_CHARACTERISTIC_UUID);
    if (m_command_value_handle == INVALID_ATTRIBUTE_HANDLE)
    {
        LOG_CRITICAL("BLE command characteristic value handle not found");
        cleanup();
        return false;
    }

    hci_event_callback_registration.callback = hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    if (!configure_advertising())
    {
        LOG_CRITICAL("BLE advertising configuration failed");
        cleanup();
        return false;
    }

    hci_power_control(HCI_POWER_ON);

    m_initialized = true;
    LOG_INFO("Remote link advertising BLE device '%s'", m_device_name);
    return true;
}

bool RemoteLink::configure_advertising()
{
    std::size_t advertising_data_length = 0;

    if (!append_advertising_field(m_advertising_data,
                                  MAX_ADVERTISING_DATA_BYTES,
                                  advertising_data_length,
                                  BLUETOOTH_DATA_TYPE_FLAGS,
                                  &ADVERTISING_FLAGS,
                                  sizeof(ADVERTISING_FLAGS)))
    {
        return false;
    }

    if (m_device_name == nullptr)
        return false;

    const std::size_t device_name_length = std::strlen(m_device_name);
    if (!append_advertising_field(m_advertising_data,
                                  MAX_ADVERTISING_DATA_BYTES,
                                  advertising_data_length,
                                  BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
                                  reinterpret_cast<const std::uint8_t*>(m_device_name),
                                  device_name_length))
    {
        return false;
    }

    std::uint8_t service_uuid_little_endian[BLE_UUID_BYTES]{};
    reverse_uuid(common::BLE_SERVICE_UUID, service_uuid_little_endian);
    if (!append_advertising_field(m_advertising_data,
                                  MAX_ADVERTISING_DATA_BYTES,
                                  advertising_data_length,
                                  BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
                                  service_uuid_little_endian,
                                  sizeof(service_uuid_little_endian)))
    {
        return false;
    }

    m_advertising_data_length = static_cast<std::uint8_t>(advertising_data_length);

    bd_addr_t null_address{};
    gap_advertisements_set_params(ADVERTISING_INTERVAL_MIN_UNITS,
                                  ADVERTISING_INTERVAL_MAX_UNITS,
                                  ADVERTISING_TYPE_CONNECTABLE_UNDIRECTED,
                                  0,
                                  null_address,
                                  ADVERTISING_CHANNEL_MAP_ALL,
                                  ADVERTISING_FILTER_POLICY_ANY);
    gap_advertisements_set_data(m_advertising_data_length, m_advertising_data);
    gap_advertisements_enable(true);
    return true;
}

void RemoteLink::hci_packet_handler(const std::uint8_t p_packet_type,
                                    const std::uint16_t p_channel,
                                    std::uint8_t* const p_packet,
                                    const std::uint16_t p_size)
{
    (void)p_channel;
    (void)p_size;

    if (active_remote_link == nullptr || p_packet_type != HCI_EVENT_PACKET)
        return;

    if (hci_event_packet_get_type(p_packet) == BTSTACK_EVENT_STATE &&
        btstack_event_state_get_state(p_packet) == HCI_STATE_WORKING)
    {
        active_remote_link->handle_stack_ready();
    }
}

void RemoteLink::att_packet_handler(const std::uint8_t p_packet_type,
                                    const std::uint16_t p_channel,
                                    std::uint8_t* const p_packet,
                                    const std::uint16_t p_size)
{
    (void)p_channel;
    (void)p_size;

    if (active_remote_link == nullptr || p_packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(p_packet))
    {
    case ATT_EVENT_CONNECTED:
        active_remote_link->handle_connected(att_event_connected_get_handle(p_packet));
        break;
    case ATT_EVENT_DISCONNECTED:
        active_remote_link->handle_disconnected(att_event_disconnected_get_handle(p_packet));
        break;
    default:
        break;
    }
}

int RemoteLink::att_write_callback(const std::uint16_t p_connection_handle,
                                   const std::uint16_t p_attribute_handle,
                                   const std::uint16_t p_transaction_mode,
                                   const std::uint16_t p_offset,
                                   std::uint8_t* const p_buffer,
                                   const std::uint16_t p_buffer_size)
{
    (void)p_connection_handle;
    (void)p_offset;

    if (active_remote_link == nullptr || p_transaction_mode != ATT_TRANSACTION_MODE_NONE)
        return 0;

    active_remote_link->handle_write(p_attribute_handle, p_buffer, p_buffer_size);
    return 0;
}

void RemoteLink::handle_stack_ready()
{
    critical_section_enter_blocking(&m_lock);
    {
        if (m_connection_handle == INVALID_CONNECTION_HANDLE)
            m_state = State::Advertising;
    }
    critical_section_exit(&m_lock);
}

void RemoteLink::handle_connected(const std::uint16_t p_connection_handle)
{
    critical_section_enter_blocking(&m_lock);
    {
        m_connection_handle = p_connection_handle;
        m_state = State::Connected;
    }
    critical_section_exit(&m_lock);
}

void RemoteLink::handle_disconnected(const std::uint16_t p_connection_handle)
{
    critical_section_enter_blocking(&m_lock);
    {
        if (m_connection_handle == p_connection_handle)
            m_connection_handle = INVALID_CONNECTION_HANDLE;
        m_state = State::Advertising;
    }
    critical_section_exit(&m_lock);
}

void RemoteLink::handle_write(const std::uint16_t p_attribute_handle,
                              const std::uint8_t* const p_buffer,
                              const std::uint16_t p_buffer_size)
{
    if (p_attribute_handle != m_command_value_handle || p_buffer == nullptr)
        return;

    Packet latest_packet{};
    latest_packet.m_received_ms = to_ms_since_boot(get_absolute_time());
    latest_packet.m_total_length = p_buffer_size;
    latest_packet.m_copied_length = static_cast<std::uint16_t>(
        std::min<std::size_t>(p_buffer_size, MAX_PACKET_BYTES));
    latest_packet.m_truncated = latest_packet.m_copied_length < latest_packet.m_total_length;

    if (latest_packet.m_copied_length > 0)
        std::memcpy(latest_packet.m_payload, p_buffer, latest_packet.m_copied_length);

    critical_section_enter_blocking(&m_lock);
    {
        if (m_has_packet)
            ++m_overwritten_packets;
        latest_packet.m_overwritten_packets = m_overwritten_packets;
        m_packet = latest_packet;
        m_has_packet = true;
    }
    critical_section_exit(&m_lock);
}

bool RemoteLink::get_packet(Packet& p_packet)
{
    bool has_packet = false;

    critical_section_enter_blocking(&m_lock);
    {
        has_packet = m_has_packet;
        if (has_packet)
        {
            p_packet = m_packet;
            m_has_packet = false;
            m_overwritten_packets = 0;
        }
    }
    critical_section_exit(&m_lock);

    return has_packet;
}

RemoteLink::State RemoteLink::state() const
{
    State current_state = State::Uninitialized;

    critical_section_enter_blocking(&m_lock);
    {
        current_state = m_state;
    }
    critical_section_exit(&m_lock);

    return current_state;
}

bool RemoteLink::is_connected() const
{
    bool connected = false;

    critical_section_enter_blocking(&m_lock);
    {
        connected = m_connection_handle != INVALID_CONNECTION_HANDLE;
    }
    critical_section_exit(&m_lock);

    return connected;
}

void RemoteLink::cleanup()
{
    if (!m_initialized && !m_cyw43_initialized)
        return;

    if (m_initialized)
    {
        gap_advertisements_enable(false);
        hci_remove_event_handler(&hci_event_callback_registration);
        att_server_deinit();
        hci_power_control(HCI_POWER_OFF);
        m_initialized = false;
    }

    if (m_cyw43_initialized)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        cyw43_arch_deinit();
        m_cyw43_initialized = false;
    }

    critical_section_enter_blocking(&m_lock);
    {
        m_state = State::Uninitialized;
        m_connection_handle = INVALID_CONNECTION_HANDLE;
        m_command_value_handle = INVALID_ATTRIBUTE_HANDLE;
        m_has_packet = false;
        m_overwritten_packets = 0;
    }
    critical_section_exit(&m_lock);

    if (active_remote_link == this)
        active_remote_link = nullptr;
}
