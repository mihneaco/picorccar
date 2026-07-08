# Improvements

## Add proper handling for diagonals

## Improve reconnect mechanism
Controller `is_connected()` keys off `m_udp_pcb != nullptr`, which survives a Wi-Fi link
loss, so it never re-associates after a transient drop. Reconnect based on CYW43 link status (`cyw43_tcpip_link_status` / `cyw43_wifi_link_status`) instead.

# Bugs
