#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "advchat/message.hpp"

namespace advchat {

class CausalBroadcast {
public:
    using DeliverFn = std::function<void(const Message&)>;
    using SendFn    = std::function<void(const Message&)>;

    CausalBroadcast(std::string self_id, DeliverFn on_deliver, SendFn on_send);

    Message build(MsgType type, const std::string& name,
                  const std::string& text, const std::string& target = "");

    void send_local(const std::string& name, const std::string& text);
    void send_poker(const std::string& name, const std::string& cmd);
    void send_private(const std::string& name, const std::string& to_id,
                      const std::string& text);
    void send_leave(const std::string& name);

    void receive(const Message& m);

private:
    bool causally_ready_locked(const Message& m) const;
    void drain_locked(std::vector<Message>& out);

    std::string self_id_;
    DeliverFn   on_deliver_;
    SendFn      on_send_;
    std::mutex  mu_;
    VectorClock vc_;
    uint64_t    lamport_ = 0;
    std::map<std::string, Message> buffer_;
    std::set<std::string>          delivered_;
};

} // namespace advchat
