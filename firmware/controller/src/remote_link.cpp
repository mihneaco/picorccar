#include "remote_link.h"

#include "ble/ble_conf.h"
#include "pico_logger.h"

#include <algorithm>
#include <cstring>

#include "btstack.h"
#include "cyw43.h"
#include "pico/cyw43_arch.h"

namespace
{
constexpr std::size_t BLE_ADDRESS_BYTES = 6;
constexpr std::size_t BLE_UUID_BYTES = 16;
constexpr std::uint16_t SCAN_INTERVAL_UNITS = 0x0030;
constexpr std::uint16_t SCAN_WINDOW_UNITS = 0x0030;
constexpr std::uint8_t SCAN_TYPE_PASSIVE = 0;
constexpr std::size_t ADVERTISING_FIELD_TYPE_BYTES = 1;
}

RemoteLink* RemoteLink::m_callback_remote_link = nullptr;

RemoteLink::RemoteLink(const char* const p_peer_name)
    : m_peer_name(p_peer_name)
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

    LOG_INFO("initializing CYW43/BLE");
    const int cyw43_init_result = cyw43_arch_init();
    if (cyw43_init_result != 0)
    {
        LOG_CRITICAL("CYW43/BLE init failed: %d", cyw43_init_result);
        return false;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    m_cyw43_initialized = true;
    m_callback_remote_link = this;

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gatt_client_init();

    m_hci_event_callback_registration.callback = hci_packet_handler;
    hci_add_event_handler(&m_hci_event_callback_registration);

    m_initialized = true;
    hci_power_control(HCI_POWER_ON);

    LOG_INFO("Remote link scanning for BLE peer '%s'", m_peer_name);
    return true;
}

void RemoteLink::hci_packet_handler(const std::uint8_t p_packet_type,
                                    const std::uint16_t p_channel,
                                    std::uint8_t* const p_packet,
                                    const std::uint16_t p_size)
{
    (void)p_channel;
    (void)p_size;

    if (m_callback_remote_link == nullptr || p_packet_type != HCI_EVENT_PACKET)
        return;

    bd_addr_t peer_address{};
    const std::uint8_t* advertising_data = nullptr;
    std::uint8_t advertising_data_length = 0;
    std::uint16_t connection_handle = INVALID_CONNECTION_HANDLE;

    switch (hci_event_packet_get_type(p_packet))
    {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(p_packet) == HCI_STATE_WORKING)
            m_callback_remote_link->start_scan();
        break;

    case GAP_EVENT_ADVERTISING_REPORT:
        advertising_data = gap_event_advertising_report_get_data(p_packet);
        advertising_data_length = gap_event_advertising_report_get_data_length(p_packet);
        gap_event_advertising_report_get_address(p_packet, peer_address);
        m_callback_remote_link->handle_advertising_report(advertising_data,
                                                          advertising_data_length,
                                                          peer_address,
                                                          gap_event_advertising_report_get_address_type(p_packet));
        break;

    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(p_packet) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE)
        {
            m_callback_remote_link->handle_connection_complete(
                gap_subevent_le_connection_complete_get_status(p_packet),
                gap_subevent_le_connection_complete_get_connection_handle(p_packet));
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
        connection_handle = hci_event_disconnection_complete_get_connection_handle(p_packet);
        m_callback_remote_link->handle_disconnected(connection_handle);
        break;

    default:
        break;
    }
}

void RemoteLink::gatt_packet_handler(const std::uint8_t p_packet_type,
                                     const std::uint16_t p_channel,
                                     std::uint8_t* const p_packet,
                                     const std::uint16_t p_size)
{
    (void)p_channel;
    (void)p_size;

    if (m_callback_remote_link == nullptr || p_packet_type != HCI_EVENT_PACKET)
        return;

    m_callback_remote_link->handle_gatt_event(p_packet);
}

void RemoteLink::start_scan()
{
    if (!m_initialized)
        return;

    set_state(State::Scanning);
    gap_set_scan_parameters(SCAN_TYPE_PASSIVE, SCAN_INTERVAL_UNITS, SCAN_WINDOW_UNITS);
    gap_start_scan();
}

void RemoteLink::handle_advertising_report(const std::uint8_t* const p_advertising_data,
                                           const std::uint8_t p_advertising_data_length,
                                           const std::uint8_t* const p_address,
                                           const std::uint8_t p_address_type)
{
    if (p_advertising_data == nullptr || p_address == nullptr || state() != State::Scanning)
        return;

    if (!advertising_data_matches_peer(p_advertising_data, p_advertising_data_length))
        return;

    bd_addr_t peer_address{};
    static_assert(sizeof(peer_address) == BLE_ADDRESS_BYTES);
    std::memcpy(peer_address, p_address, sizeof(peer_address));

    gap_stop_scan();
    set_state(State::Connecting);
    gap_connect(peer_address, static_cast<bd_addr_type_t>(p_address_type));
}

bool RemoteLink::advertising_data_matches_peer(const std::uint8_t* const p_advertising_data,
                                               const std::uint8_t p_advertising_data_length) const
{
    if (p_advertising_data == nullptr)
        return false;

    const bool peer_name_is_any = m_peer_name == nullptr;
    const std::size_t peer_name_length = peer_name_is_any ? 0 : std::strlen(m_peer_name);
    bool found_service_uuid = false;
    bool found_peer_name = peer_name_is_any;

    std::size_t offset = 0;
    while (offset < p_advertising_data_length)
    {
        const std::uint8_t field_length = p_advertising_data[offset++];
        if (field_length == 0)
            break;
        if (field_length < ADVERTISING_FIELD_TYPE_BYTES || offset + field_length > p_advertising_data_length)
            return false;

        const std::uint8_t field_type = p_advertising_data[offset++];
        const std::size_t value_length = field_length - ADVERTISING_FIELD_TYPE_BYTES;
        const std::uint8_t* const value = &p_advertising_data[offset];

        if (field_type == BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS ||
            field_type == BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS)
        {
            for (std::size_t value_offset = 0; value_offset + BLE_UUID_BYTES <= value_length; value_offset += BLE_UUID_BYTES)
            {
                bool uuid_matches = true;
                for (std::size_t uuid_byte = 0; uuid_byte < BLE_UUID_BYTES; ++uuid_byte)
                {
                    if (value[value_offset + uuid_byte] != common::BLE_SERVICE_UUID[BLE_UUID_BYTES - 1 - uuid_byte])
                    {
                        uuid_matches = false;
                        break;
                    }
                }

                if (uuid_matches)
                {
                    found_service_uuid = true;
                    break;
                }
            }
        }
        else if (!peer_name_is_any &&
                 (field_type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME ||
                  field_type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME) &&
                 value_length == peer_name_length &&
                 std::memcmp(value, m_peer_name, peer_name_length) == 0)
        {
            found_peer_name = true;
        }

        if (found_service_uuid && found_peer_name)
            return true;

        offset += value_length;
    }

    return false;
}

void RemoteLink::handle_connection_complete(const std::uint8_t p_status,
                                            const std::uint16_t p_connection_handle)
{
    if (state() != State::Connecting)
        return;

    if (p_status != ERROR_CODE_SUCCESS)
    {
        start_scan();
        return;
    }

    critical_section_enter_blocking(&m_lock);
    {
        m_connection_handle = p_connection_handle;
        m_command_value_handle = INVALID_ATTRIBUTE_HANDLE;
        m_service_found = false;
        m_command_found = false;
        m_has_pending_packet = false;
        m_write_request_pending = false;
        m_state = State::DiscoveringService;
    }
    critical_section_exit(&m_lock);

    const std::uint8_t discover_result = gatt_client_discover_primary_services_by_uuid128(
        gatt_packet_handler,
        p_connection_handle,
        common::BLE_SERVICE_UUID);
    if (discover_result != ERROR_CODE_SUCCESS)
        gap_disconnect(p_connection_handle);
}

void RemoteLink::handle_disconnected(const std::uint16_t p_connection_handle)
{
    bool should_scan = false;

    critical_section_enter_blocking(&m_lock);
    {
        if (m_connection_handle == p_connection_handle || m_connection_handle == INVALID_CONNECTION_HANDLE)
        {
            m_connection_handle = INVALID_CONNECTION_HANDLE;
            m_command_value_handle = INVALID_ATTRIBUTE_HANDLE;
            m_service_found = false;
            m_command_found = false;
            m_has_pending_packet = false;
            m_write_request_pending = false;
            should_scan = m_initialized;
        }
    }
    critical_section_exit(&m_lock);

    if (should_scan)
        start_scan();
}

void RemoteLink::handle_gatt_event(std::uint8_t* const p_packet)
{
    const std::uint8_t event_type = hci_event_packet_get_type(p_packet);
    const State current_state = state();

    if (current_state == State::DiscoveringService)
    {
        switch (event_type)
        {
        case GATT_EVENT_SERVICE_QUERY_RESULT:
            gatt_event_service_query_result_get_service(p_packet, &m_remote_service);
            m_service_found = true;
            break;

        case GATT_EVENT_QUERY_COMPLETE:
            if (gatt_event_query_complete_get_att_status(p_packet) != ATT_ERROR_SUCCESS || !m_service_found)
            {
                gap_disconnect(m_connection_handle);
                break;
            }

            set_state(State::DiscoveringCommand);
            if (gatt_client_discover_characteristics_for_service_by_uuid128(gatt_packet_handler,
                                                                            m_connection_handle,
                                                                            &m_remote_service,
                                                                            common::BLE_COMMAND_CHARACTERISTIC_UUID) !=
                ERROR_CODE_SUCCESS)
            {
                gap_disconnect(m_connection_handle);
            }
            break;

        default:
            break;
        }
        return;
    }

    if (current_state == State::DiscoveringCommand)
    {
        switch (event_type)
        {
        case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
            gatt_event_characteristic_query_result_get_characteristic(p_packet, &m_command_characteristic);
            critical_section_enter_blocking(&m_lock);
            {
                m_command_value_handle = m_command_characteristic.value_handle;
                m_command_found = true;
            }
            critical_section_exit(&m_lock);
            break;

        case GATT_EVENT_QUERY_COMPLETE:
            if (gatt_event_query_complete_get_att_status(p_packet) != ATT_ERROR_SUCCESS || !m_command_found)
            {
                gap_disconnect(m_connection_handle);
                break;
            }

            set_state(State::Ready);
            break;

        default:
            break;
        }
    }
}

bool RemoteLink::send_packet(const std::uint8_t* const p_payload, const std::size_t p_length)
{
    if (p_payload == nullptr)
    {
        LOG_WARNING("Refusing to send null BLE payload");
        return false;
    }

    if (p_length == 0)
    {
        LOG_WARNING("Refusing to send empty BLE packet");
        return false;
    }

    if (p_length > MAX_PACKET_BYTES)
    {
        LOG_WARNING("BLE payload too large: %u > %u",
                    static_cast<unsigned>(p_length),
                    static_cast<unsigned>(MAX_PACKET_BYTES));
        return false;
    }

    bool should_request_write = false;
    bool accepted = false;

    critical_section_enter_blocking(&m_lock);
    {
        if (m_state == State::Ready && m_connection_handle != INVALID_CONNECTION_HANDLE &&
            m_command_value_handle != INVALID_ATTRIBUTE_HANDLE)
        {
            std::memcpy(m_pending_payload, p_payload, p_length);
            m_pending_length = static_cast<std::uint16_t>(p_length);
            m_has_pending_packet = true;
            accepted = true;

            if (!m_write_request_pending)
            {
                m_write_request_pending = true;
                should_request_write = true;
            }
        }
    }
    critical_section_exit(&m_lock);

    if (should_request_write)
        request_write();

    if (!accepted)
        LOG_WARNING("Remote link send requested before BLE link is ready");

    return accepted;
}

void RemoteLink::request_write()
{
    m_request_write_registration.callback = request_write_callback;
    m_request_write_registration.context = this;
    btstack_run_loop_execute_on_main_thread(&m_request_write_registration);
}

void RemoteLink::request_write_callback(void* const p_context)
{
    auto* const this_ref = static_cast<RemoteLink*>(p_context);
    if (this_ref == nullptr)
        return;

    std::uint16_t connection_handle = INVALID_CONNECTION_HANDLE;
    bool ready = false;

    critical_section_enter_blocking(&this_ref->m_lock);
    {
        ready = this_ref->m_state == State::Ready &&
                this_ref->m_connection_handle != INVALID_CONNECTION_HANDLE &&
                this_ref->m_has_pending_packet;
        connection_handle = this_ref->m_connection_handle;
        if (!ready)
            this_ref->m_write_request_pending = false;
    }
    critical_section_exit(&this_ref->m_lock);

    if (!ready)
        return;

    this_ref->m_write_ready_registration.callback = write_ready_callback;
    this_ref->m_write_ready_registration.context = this_ref;
    const std::uint8_t request_result =
        gatt_client_request_to_write_without_response(&this_ref->m_write_ready_registration, connection_handle);
    if (request_result != ERROR_CODE_SUCCESS)
    {
        critical_section_enter_blocking(&this_ref->m_lock);
        {
            this_ref->m_write_request_pending = false;
        }
        critical_section_exit(&this_ref->m_lock);
    }
}

void RemoteLink::write_ready_callback(void* const p_context)
{
    auto* const this_ref = static_cast<RemoteLink*>(p_context);
    if (this_ref != nullptr)
        this_ref->handle_write_ready();
}

void RemoteLink::handle_write_ready()
{
    std::uint8_t payload[MAX_PACKET_BYTES]{};
    std::uint16_t payload_length = 0;
    std::uint16_t connection_handle = INVALID_CONNECTION_HANDLE;
    std::uint16_t command_value_handle = INVALID_ATTRIBUTE_HANDLE;
    bool should_send = false;

    critical_section_enter_blocking(&m_lock);
    {
        should_send = m_state == State::Ready &&
                      m_connection_handle != INVALID_CONNECTION_HANDLE &&
                      m_command_value_handle != INVALID_ATTRIBUTE_HANDLE &&
                      m_has_pending_packet;

        if (should_send)
        {
            payload_length = m_pending_length;
            connection_handle = m_connection_handle;
            command_value_handle = m_command_value_handle;
            std::memcpy(payload, m_pending_payload, payload_length);
            m_has_pending_packet = false;
        }

        m_write_request_pending = false;
    }
    critical_section_exit(&m_lock);

    if (!should_send)
        return;

    gatt_client_write_value_of_characteristic_without_response(connection_handle,
                                                              command_value_handle,
                                                              payload_length,
                                                              payload);
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

bool RemoteLink::is_ready() const
{
    bool ready = false;

    critical_section_enter_blocking(&m_lock);
    {
        ready = m_state == State::Ready &&
                m_connection_handle != INVALID_CONNECTION_HANDLE &&
                m_command_value_handle != INVALID_ATTRIBUTE_HANDLE;
    }
    critical_section_exit(&m_lock);

    return ready;
}

void RemoteLink::set_state(const State p_state)
{
    critical_section_enter_blocking(&m_lock);
    {
        m_state = p_state;
    }
    critical_section_exit(&m_lock);
}

void RemoteLink::cleanup()
{
    if (!m_initialized && !m_cyw43_initialized)
        return;

    if (m_initialized)
    {
        if (state() == State::Scanning)
            gap_stop_scan();
        if (m_connection_handle != INVALID_CONNECTION_HANDLE)
            gap_disconnect(m_connection_handle);

        hci_remove_event_handler(&m_hci_event_callback_registration);
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
        m_service_found = false;
        m_command_found = false;
        m_has_pending_packet = false;
        m_write_request_pending = false;
    }
    critical_section_exit(&m_lock);

    if (m_callback_remote_link == this)
        m_callback_remote_link = nullptr;
}
