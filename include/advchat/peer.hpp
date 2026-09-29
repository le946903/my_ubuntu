#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace advchat {

struct Peer {
    std::string name;
    std::string ip;
    int port = 0;
    int sock = -1;
    std::chrono::steady_clock::time_point last_seen;
};

std::unordered_map<std::string, Peer>& peers();
extern std::mutex g_peers_mu;

} // namespace advchat
