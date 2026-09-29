#include "advchat/net_io.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "advchat/causal_broadcast.hpp"
#include "advchat/config.hpp"
#include "advchat/crypto.hpp"
#include "advchat/helpers.hpp"

namespace advchat {

std::atomic<bool> g_running{true};
std::string       g_my_name;
std::string       g_my_id_hex;
Id                g_my_id{};
int               g_my_tcp_port = 0;
CausalBroadcast*  g_cb = nullptr;

bool send_all(int sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(sock, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

void net_broadcast(const Message& m) {
    std::string wire = m.serialize();
    dlog("NET >> broadcast type=", msg_type_str(m.type),
         " from=", m.name, " text=", m.text);
    std::vector<int> socks;
    {
        std::lock_guard<std::mutex> lk(g_peers_mu);
        for (auto& [id, p] : peers()) if (p.sock >= 0) socks.push_back(p.sock);
    }
    std::sort(socks.begin(), socks.end());
    socks.erase(std::unique(socks.begin(), socks.end()), socks.end());
    for (int s : socks) send_all(s, wire);
}

std::string peer_ip_from_sock(int sock) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(sock, (sockaddr*)&addr, &len) != 0) return "";
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    return buf;
}

void reader_loop(int sock, std::string peer_hex_id) {
    std::string buf;
    char tmp[4096];
    while (g_running) {
        ssize_t n = ::recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (line.empty()) continue;

            if (line.rfind("HELLO|", 0) == 0) {
                std::vector<std::string> p;
                size_t start = 6;
                while (start <= line.size()) {
                    size_t b = line.find('|', start);
                    p.push_back(line.substr(start, (b == std::string::npos)
                                                      ? std::string::npos
                                                      : b - start));
                    if (b == std::string::npos) break;
                    start = b + 1;
                }
                if (p.size() >= 3) {
                    std::string real_id   = p[0];
                    std::string real_name = p[1];
                    int real_port = std::atoi(p[2].c_str());
                    std::string ip = peer_ip_from_sock(sock);
                    {
                        std::lock_guard<std::mutex> lk(g_peers_mu);
                        if (peer_hex_id.rfind("incoming:", 0) == 0)
                            peers().erase(peer_hex_id);
                        auto it = peers().find(real_id);
                        if (it != peers().end()) {
                            it->second.name      = real_name;
                            it->second.port      = real_port;
                            it->second.ip        = ip;
                            it->second.sock      = sock;
                            it->second.last_seen = std::chrono::steady_clock::now();
                        } else {
                            Peer peer;
                            peer.name      = real_name;
                            peer.ip        = ip;
                            peer.port      = real_port;
                            peer.sock      = sock;
                            peer.last_seen = std::chrono::steady_clock::now();
                            peers()[real_id] = peer;
                        }
                    }
                    dlog("PEER identified ", real_name, " (", real_id.substr(0, 8), ")");
                    say("\r[+] identified peer ", real_name,
                        " (", real_id.substr(0, 8), "...)\n> ");
                }
                continue;
            }

            Message m;
            if (Message::parse(line, m)) {
                std::string expect = Message::compute_hash(
                    m.type, m.id, m.name, m.target, m.lamport, m.vc, m.text);
                if (expect != m.hash) {
                    dlog("SECURITY bad hash from ", m.name);
                    say("\r[!] Bad hash from ", m.name, " -- dropping\n> ");
                    continue;
                }
                dlog("NET << recv type=", msg_type_str(m.type),
                     " from=", m.name, " text=", m.text);
                if (g_cb) g_cb->receive(m);
            }
        }
    }
    ::close(sock);
    {
        std::lock_guard<std::mutex> lk(g_peers_mu);
        for (auto& [id, p] : peers()) if (p.sock == sock) p.sock = -1;
    }
}

void connect_to_peer(const std::string& peer_hex_id,
                     const std::string& name,
                     const std::string& ip, int port) {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        ::close(sock); return;
    }
    if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(sock); return;
    }

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    {
        std::lock_guard<std::mutex> lk(g_peers_mu);
        auto& p = peers()[peer_hex_id];
        p.name = name; p.ip = ip; p.port = port; p.sock = sock;
        p.last_seen = std::chrono::steady_clock::now();
    }
    std::string hello = "HELLO|" + g_my_id_hex + "|" + g_my_name + "|"
                      + std::to_string(g_my_tcp_port) + "\n";
    send_all(sock, hello);
    dlog("PEER connected to ", name, " @ ", ip, ":", port);
    say("\r[+] connected to ", name, " (", ip, ":", port, ")\n> ");
    std::thread(reader_loop, sock, peer_hex_id).detach();
}

void tcp_accept_loop(int listen_sock) {
    while (g_running) {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int csock = ::accept(listen_sock, (sockaddr*)&caddr, &clen);
        if (csock < 0) {
            if (!g_running) break;
            continue;
        }

        int one = 1;
        setsockopt(csock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
        std::string temp_id = "incoming:" + std::string(ip) + ":" + std::to_string(csock);
        {
            std::lock_guard<std::mutex> lk(g_peers_mu);
            Peer p;
            p.name = "unknown"; p.ip = ip; p.sock = csock;
            p.last_seen = std::chrono::steady_clock::now();
            peers()[temp_id] = p;
        }
        dlog("PEER incoming connection from ", ip);
        say("\r[+] incoming connection from ", ip, "\n> ");
        std::thread(reader_loop, csock, temp_id).detach();
    }
}

int setup_multicast_socket() {
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MULTICAST_PORT);
    if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); ::close(sock); return -1;
    }
    ip_mreq mreq{};
    inet_pton(AF_INET, MULTICAST_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("IP_ADD_MEMBERSHIP"); ::close(sock); return -1;
    }
    unsigned char loop = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    return sock;
}

void udp_announce_loop(int sock) {
    sockaddr_in maddr{};
    maddr.sin_family = AF_INET;
    maddr.sin_port = htons(MULTICAST_PORT);
    inet_pton(AF_INET, MULTICAST_ADDR, &maddr.sin_addr);
    std::string hello = "ANNOUNCE|" + g_my_id_hex + "|" + g_my_name + "|"
                      + std::to_string(g_my_tcp_port) + "\n";
    while (g_running) {
        ::sendto(sock, hello.data(), hello.size(), 0,
                 (sockaddr*)&maddr, sizeof(maddr));
        for (int i = 0; i < HELLO_INTERVAL_SEC * 10 && g_running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void udp_listen_loop(int sock) {
    char buf[2048];
    while (g_running) {
        sockaddr_in saddr{};
        socklen_t slen = sizeof(saddr);
        ssize_t n = ::recvfrom(sock, buf, sizeof(buf) - 1, 0,
                               (sockaddr*)&saddr, &slen);
        if (n <= 0) { if (!g_running) break; continue; }
        buf[n] = 0;
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.rfind("ANNOUNCE|", 0) != 0) continue;

        std::vector<std::string> p;
        size_t start = 9;
        while (start <= line.size()) {
            size_t b = line.find('|', start);
            p.push_back(line.substr(start, (b == std::string::npos)
                                              ? std::string::npos
                                              : b - start));
            if (b == std::string::npos) break;
            start = b + 1;
        }
        if (p.size() < 3) continue;
        std::string peer_id_hex = p[0];
        std::string peer_name   = p[1];
        int peer_port = std::atoi(p[2].c_str());
        if (peer_id_hex == g_my_id_hex) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &saddr.sin_addr, ip, sizeof(ip));
        std::string peer_ip = ip;

        bool i_dial = (g_my_id_hex < peer_id_hex);
        bool need_connect = false;
        {
            std::lock_guard<std::mutex> lk(g_peers_mu);
            auto it = peers().find(peer_id_hex);
            if (it != peers().end()) {
                it->second.last_seen = std::chrono::steady_clock::now();
                it->second.name = peer_name;
                it->second.port = peer_port;
                if (it->second.sock < 0 && i_dial) need_connect = true;
            } else {
                need_connect = i_dial;
            }
        }
        if (need_connect) connect_to_peer(peer_id_hex, peer_name, peer_ip, peer_port);
    }
}

void reaper_loop() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> removed_names;
        {
            std::lock_guard<std::mutex> lk(g_peers_mu);
            for (auto it = peers().begin(); it != peers().end(); ) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->second.last_seen).count();
                bool is_real = it->first.rfind("incoming:", 0) != 0;
                if (age > PEER_TIMEOUT_SEC && it->second.sock < 0 && is_real) {
                    if (it->second.name != "unknown" && !it->second.name.empty())
                        removed_names.push_back(it->second.name);
                    it = peers().erase(it);
                } else ++it;
            }
        }
        for (auto& n : removed_names)
            say("\r[", now_hhmmss(), "] * ", n, " disconnected (timeout)\n> ");
    }
}

} // namespace advchat
