#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "advchat/card.hpp"
#include "advchat/message.hpp"

namespace advchat {

class CausalBroadcast;

enum class PokerPhase { IDLE, PREFLOP, FLOP, TURN, RIVER, SHOWDOWN, ENDED };

struct PokerPlayer {
    std::string id, name;
    int chips = 1000;
    int chips_at_hand_start = 1000;
    Card hole[2];
    bool folded = false;
    bool all_in = false;
    int  committed_this_street = 0;
    int  committed_total = 0;
    bool in_hand = false;
    bool has_acted_this_street = false;
    std::string commit_hash;
    std::string reveal_seed;
    bool revealed = false;
    bool shown = false;
};

class PokerGame {
public:
    std::string self_id;
    std::string self_name;
    CausalBroadcast* cb = nullptr;

    std::map<std::string, PokerPlayer> players;
    PokerPhase phase = PokerPhase::IDLE;
    int pot = 0;
    int current_bet = 0;
    int small_blind = 10;
    int big_blind = 20;
    std::string dealer_id;
    std::string sb_id;
    std::string bb_id;
    std::string turn_id;
    std::vector<Card> community;
    std::vector<Card> deck;
    int hand_number = 0;
    bool hand_in_progress = false;
    bool waiting_for_reveals = false;
    bool showdown_processed = false;
    std::string combined_seed;
    size_t deck_pos = 0;

    bool game_running = false;
    std::string game_leader_id;

    std::function<void(const std::string&)> log;
    std::string my_pending_seed_;

    std::recursive_mutex pmu_;

    void handle_message(const Message& m);
    std::string render_box_locked() const;
    void print_turn_box_locked();

private:
    std::vector<std::string> seat_order_locked() const;
    std::string next_active_locked(const std::string& from) const;
    std::string random_seed_hex();

    void handle_poker_locked(const Message& m);
    void handle_private_locked(const Message& m);
    void handle_action_locked(const Message& m, const std::string& payload);

    void maybe_ask_reveal_locked();
    void maybe_finalize_shuffle_locked();
    void post_blind_locked(const std::string& id, int amt);
    void announce_turn_locked();
    void advance_if_needed_locked();
    void advance_street_locked();
    std::string board_string_locked() const;
    void do_showdown_locked();
    void parse_showdown_locked(const std::string& payload);
    void end_hand_locked();
    void start_hand_locked();
};

} // namespace advchat
