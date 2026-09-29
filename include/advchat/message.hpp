#pragma once
#include <cstdint>
#include <string>

#include "advchat/vector_clock.hpp"

namespace advchat {

enum class MsgType { CHAT, LEAVE, POKER, PRIV };

const char* msg_type_str(MsgType t);
MsgType     parse_msg_type(const std::string& s);

struct Message {
    MsgType     type = MsgType::CHAT;
    std::string id;
    std::string name;
    std::string target;
    uint64_t    lamport = 0;
    VectorClock vc;
    std::string text;
    std::string hash;

    std::string serialize() const;
    static bool parse(const std::string& line, Message& out);

    static std::string compute_hash(MsgType type,
                                    const std::string& id,
                                    const std::string& name,
                                    const std::string& target,
                                    uint64_t lamport,
                                    const VectorClock& vc,
                                    const std::string& text);
};

} // namespace advchat
