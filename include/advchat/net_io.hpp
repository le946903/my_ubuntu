#pragma once
#include <atomic>
#include <string>

#include "advchat/crypto.hpp"
#include "advchat/message.hpp"
#include "advchat/peer.hpp"

namespace advchat {

class CausalBroadcast;

extern std::atomic<bool> g_running;
extern std::string       g_my_name;
extern std::string       g_my_id_hex;
extern Id                g_my_id;
extern int               g_my_tcp_port;
extern CausalBroadcast*  g_cb;

bool send_all(int sock, const std::string& data);
void net_broadcast(const Message& m);

std::string peer_ip_from_sock(int sock);
void        reader_loop(int sock, std::string peer_hex_id);
void        connect_to_peer(const std::string& peer_hex_id,
                            const std::string& name,
                            const std::string& ip, int port);
void        tcp_accept_loop(int listen_sock);

int  setup_multicast_socket();
void udp_announce_loop(int sock);
void udp_listen_loop(int sock);
void reaper_loop();

} // namespace advchat
