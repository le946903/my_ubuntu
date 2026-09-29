#include "advchat/causal_broadcast.hpp"

#include <algorithm>

namespace advchat {

CausalBroadcast::CausalBroadcast(std::string self_id,
                                 DeliverFn on_deliver,
                                 SendFn on_send)
    : self_id_(std::move(self_id)),
      on_deliver_(std::move(on_deliver)),
      on_send_(std::move(on_send)) {}

Message CausalBroadcast::build(MsgType type, const std::string& name,
                               const std::string& text, const std::string& target) {
    Message m;
    {
        std::lock_guard<std::mutex> lk(mu_);
        vc_.inc(self_id_);
        m.type    = type;
        m.id      = self_id_;
        m.name    = name;
        m.target  = target;
        m.lamport = ++lamport_;
        m.vc      = vc_;
        m.text    = text;
        m.hash    = Message::compute_hash(m.type, m.id, m.name, m.target,
                                          m.lamport, m.vc, m.text);
        delivered_.insert(m.hash);
    }
    on_deliver_(m);
    return m;
}

void CausalBroadcast::send_local(const std::string& name, const std::string& text) {
    Message m = build(MsgType::CHAT, name, text);
    on_send_(m);
}

void CausalBroadcast::send_poker(const std::string& name, const std::string& cmd) {
    Message m = build(MsgType::POKER, name, cmd);
    on_send_(m);
}

void CausalBroadcast::send_private(const std::string& name, const std::string& to_id,
                                   const std::string& text) {
    Message m = build(MsgType::PRIV, name, text, to_id);
    on_send_(m);
}

void CausalBroadcast::send_leave(const std::string& name) {
    Message m = build(MsgType::LEAVE, name, "");
    on_send_(m);
}

void CausalBroadcast::receive(const Message& m) {
    std::vector<Message> to_deliver;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (delivered_.count(m.hash)) return;
        if (buffer_.count(m.hash))    return;
        buffer_[m.hash] = m;
        drain_locked(to_deliver);
    }
    for (const Message& msg : to_deliver) on_deliver_(msg);
}

bool CausalBroadcast::causally_ready_locked(const Message& m) const {
    for (auto& [k, c] : m.vc.v) {
        auto it = vc_.v.find(k);
        uint64_t local = (it == vc_.v.end()) ? 0 : it->second;
        if (k == m.id) {
            if (local + 1 != c) return false;
        } else {
            if (local < c) return false;
        }
    }
    return true;
}

void CausalBroadcast::drain_locked(std::vector<Message>& out) {
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto it = buffer_.begin(); it != buffer_.end(); ) {
            const Message& m = it->second;
            if (causally_ready_locked(m)) {
                for (auto& [k, c] : m.vc.v) {
                    auto& slot = vc_.v[k];
                    if (c > slot) slot = c;
                }
                lamport_ = std::max(lamport_, m.lamport) + 1;
                delivered_.insert(m.hash);
                out.push_back(m);
                it = buffer_.erase(it);
                progress = true;
            } else {
                ++it;
            }
        }
    }
}

} // namespace advchat
