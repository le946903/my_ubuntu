#include "advchat/peer.hpp"

namespace advchat {

std::mutex g_peers_mu;
static std::unordered_map<std::string, Peer> g_peers_storage;

std::unordered_map<std::string, Peer>& peers() { return g_peers_storage; }

} // namespace advchat
