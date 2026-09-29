#include "advchat/message.hpp"

#include <openssl/sha.h>

#include <sstream>
#include <vector>

#include "advchat/helpers.hpp"

namespace advchat {

const char* msg_type_str(MsgType t) {
    switch (t) {
        case MsgType::LEAVE: return "LEAVE";
        case MsgType::POKER: return "POKER";
        case MsgType::PRIV:  return "PRIV";
        default:             return "CHAT";
    }
}

MsgType parse_msg_type(const std::string& s) {
    if (s == "LEAVE") return MsgType::LEAVE;
    if (s == "POKER") return MsgType::POKER;
    if (s == "PRIV")  return MsgType::PRIV;
    return MsgType::CHAT;
}

std::string Message::serialize() const {
    std::ostringstream os;
    os << "MSG|" << msg_type_str(type) << "|" << id << "|" << name
       << "|" << target << "|" << lamport << "|" << vc.serialize()
       << "|" << hash << "|" << text << "\n";
    return os.str();
}

bool Message::parse(const std::string& line, Message& out) {
    if (line.rfind("MSG|", 0) != 0) return false;
    std::vector<std::string> parts;
    size_t pos = 4;
    for (int i = 0; i < 7; ++i) {
        size_t p = line.find('|', pos);
        if (p == std::string::npos) return false;
        parts.push_back(line.substr(pos, p - pos));
        pos = p + 1;
    }
    parts.push_back(line.substr(pos));

    out.type    = parse_msg_type(parts[0]);
    out.id      = parts[1];
    out.name    = parts[2];
    out.target  = parts[3];
    try { out.lamport = std::stoull(parts[4]); } catch (...) { return false; }
    out.vc      = VectorClock::deserialize(parts[5]);
    out.hash    = parts[6];
    out.text    = parts[7];
    return true;
}

std::string Message::compute_hash(MsgType type,
                                  const std::string& id,
                                  const std::string& name,
                                  const std::string& target,
                                  uint64_t lamport,
                                  const VectorClock& vc,
                                  const std::string& text) {
    std::string s = std::string(msg_type_str(type)) + "|" + id + "|" + name
                  + "|" + target + "|" + std::to_string(lamport)
                  + "|" + vc.serialize() + "|" + text;
    unsigned char out[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), out);
    return hex_encode(out, SHA256_DIGEST_LENGTH);
}

} // namespace advchat
