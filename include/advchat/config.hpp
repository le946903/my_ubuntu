#pragma once

namespace advchat {

inline constexpr const char* MULTICAST_ADDR   = "239.255.42.99";
inline constexpr int         MULTICAST_PORT   = 45454;
inline constexpr int         HELLO_INTERVAL_SEC = 2;
inline constexpr int         PEER_TIMEOUT_SEC   = 10;
inline constexpr int         LEAVE_FLUSH_MS     = 300;
inline constexpr int         NEXT_HAND_DELAY_SEC = 3;
inline constexpr int         DEFAULT_CHIPS       = 1000;
inline constexpr int         BOX_WIDTH           = 60;

} // namespace advchat
