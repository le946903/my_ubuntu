#include "advchat/poker_game.hpp"

#include <fcntl.h>
#include <openssl/sha.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

#include "advchat/causal_broadcast.hpp"
#include "advchat/config.hpp"
#include "advchat/crypto.hpp"
#include "advchat/deck.hpp"
#include "advchat/hand_eval.hpp"
#include "advchat/helpers.hpp"

namespace advchat {

// ---- Seat ordering helpers ----

std::vector<std::string> PokerGame::seat_order_locked() const {
    std::vector<std::string> ids;
    for (auto& [id, p] : players) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string PokerGame::next_active_locked(const std::string& from) const {
    auto order = seat_order_locked();
    if (order.empty()) return "";
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == from) {
            for (size_t k = 1; k <= order.size(); ++k) {
                const std::string& cand = order[(i + k) % order.size()];
                auto it = players.find(cand);
                if (it == players.end()) continue;
                if (!it->second.in_hand || it->second.folded) continue;
                if (it->second.all_in) continue;
                return cand;
            }
        }
    }
    return "";
}

std::string PokerGame::random_seed_hex() {
    uint8_t buf[32] = {0};
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t r = ::read(fd, buf, 32);
        (void)r;
        ::close(fd);
    } else {
        auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        Id h = hash_id(std::to_string(t) + self_id + self_name);
        std::memcpy(buf, h.data(), 32);
    }
    return hex_encode(buf, 32);
}

// ---- Public dispatch ----

void PokerGame::handle_message(const Message& m) {
    if (m.type == MsgType::PRIV) {
        if (m.target != self_id) return;
        std::lock_guard<std::recursive_mutex> lk(pmu_);
        handle_private_locked(m);
        return;
    }
    if (m.type != MsgType::POKER) return;
    std::lock_guard<std::recursive_mutex> lk(pmu_);
    handle_poker_locked(m);
}

// ---- UI ----

std::string PokerGame::render_box_locked() const {
    std::ostringstream os;
    const size_t W = BOX_WIDTH;
    std::string bar(W, '-');
    auto emit = [&](const std::string& s) {
        os << "|" << pad_to(s, W) << "|\n";
    };
    os << "+" << bar << "+\n";

    {
        std::ostringstream h;
        h << " Hand #" << hand_number << "   Blinds "
          << small_blind << "/" << big_blind << "   Dealer: ";
        auto it = players.find(dealer_id);
        h << (it != players.end() ? it->second.name : "?");
        if (!game_running) h << "   (auto-rotation stopped)";
        emit(h.str());
    }
    {
        std::string ph;
        switch (phase) {
            case PokerPhase::PREFLOP: ph = "Preflop"; break;
            case PokerPhase::FLOP:    ph = "Flop"; break;
            case PokerPhase::TURN:    ph = "Turn"; break;
            case PokerPhase::RIVER:   ph = "River"; break;
            case PokerPhase::SHOWDOWN:ph = "Showdown"; break;
            default:                  ph = "Idle"; break;
        }
        emit(" Phase: " + ph);
    }
    emit(" Pot: " + std::to_string(pot));
    {
        std::string b = " Board: ";
        if (community.empty()) b += "(none)";
        else for (auto& c : community) b += c.ansi() + " ";
        emit(b);
    }
    os << "+" << bar << "+\n";

    for (auto& id : seat_order_locked()) {
        auto it = players.find(id);
        if (it == players.end()) continue;
        const auto& p = it->second;

        std::string marker = "  ";
        if (id == dealer_id) marker = "D ";
        if (id == turn_id && hand_in_progress) marker = "> ";

        std::ostringstream tag;
        if (id == sb_id && hand_in_progress) tag << "[SB] ";
        if (id == bb_id && hand_in_progress) tag << "[BB] ";
        if (p.folded)                          tag << "[folded] ";
        else if (p.all_in)                     tag << "[ALL-IN] ";
        else if (id == turn_id && hand_in_progress) {
            int owe = std::max(0, current_bet - p.committed_this_street);
            if (owe == 0) tag << "to act (check or bet)";
            else          tag << "to act (" << owe << " to call)";
        }

        std::ostringstream line;
        line << marker << " " << pad_to(p.name, 10)
             << " " << pad_to(std::to_string(p.chips), 6)
             << " " << tag.str();
        emit(line.str());
    }

    os << "+" << bar << "+\n";

    auto me = players.find(self_id);
    if (me != players.end() && me->second.in_hand
        && !me->second.folded && me->second.hole[0].valid) {
        std::ostringstream h;
        h << " Your hand: " << me->second.hole[0].ansi() << " "
          << me->second.hole[1].ansi();
        emit(h.str());
    } else if (me != players.end() && me->second.in_hand && me->second.folded) {
        emit(" Your hand: (folded)");
    } else if (me != players.end() && hand_in_progress) {
        emit(" Your hand: (sitting out)");
    } else if (me != players.end()) {
        emit(" Your hand: (waiting for next hand)");
    } else {
        emit(" (not seated)");
    }
    os << "+" << bar << "+\n";
    return os.str();
}

void PokerGame::print_turn_box_locked() {
    if (turn_id != self_id) return;
    log("");
    log(render_box_locked());
}

// ---- Incoming message handling ----

void PokerGame::handle_poker_locked(const Message& m) {
    std::string sub = m.text;
    std::string payload;
    size_t bar = sub.find('|');
    if (bar != std::string::npos) { payload = sub.substr(bar + 1); sub = sub.substr(0, bar); }

    if (sub == "JOIN") {
        auto existing = players.find(m.id);
        if (existing != players.end()) {
            auto& p = existing->second;
            p.chips = DEFAULT_CHIPS;
            p.chips_at_hand_start = DEFAULT_CHIPS;
            p.folded = false;
            p.in_hand = false;
            p.all_in = false;
            p.committed_this_street = 0;
            p.committed_total = 0;
            p.has_acted_this_street = false;
            p.commit_hash.clear();
            p.reveal_seed.clear();
            p.revealed = false;
            p.shown = false;
            p.hole[0] = Card{};
            p.hole[1] = Card{};
            log("*** " + m.name + " rejoined the poker table with " +
                std::to_string(DEFAULT_CHIPS) + " chips. ***");
            return;
        }
        PokerPlayer p;
        p.id = m.id;
        p.name = m.name;
        p.chips = DEFAULT_CHIPS;
        p.chips_at_hand_start = DEFAULT_CHIPS;
        players[m.id] = p;
        log("*** " + m.name + " joined the poker table (" +
            std::to_string(players.size()) + " players, " +
            std::to_string(DEFAULT_CHIPS) + " chips) ***");
    }
    else if (sub == "LEAVE_TABLE") {
        auto it = players.find(m.id);
        if (it == players.end()) return;
        if (hand_in_progress && it->second.in_hand) {
            if (!it->second.folded) {
                it->second.folded = true;
                log("*** " + m.name + " left mid-hand (auto-fold) ***");
            }
            advance_if_needed_locked();
        } else {
            players.erase(it);
            log("*** " + m.name + " left the poker table ***");
        }
    }
    else if (sub == "START") {
        if (hand_in_progress) return;
        if (players.size() < 2) { log("[poker] need at least 2 players"); return; }
        int able = 0;
        for (auto& [id, p] : players) if (p.chips > 0) able++;
        if (able < 2) { log("[poker] need at least 2 players with chips"); return; }
        game_running = true;
        game_leader_id = m.id;
        log("*** " + m.name + " started the game. Blinds auto-rotate. "
            "Type /poker stop to end. ***");
        start_hand_locked();
    }
    else if (sub == "STOP") {
        if (!game_running) return;
        game_running = false;
        log("*** Auto-rotation stopped. Current hand will finish normally. ***");
        if (!hand_in_progress) {
            log("[poker] No hand in progress; table is idle.");
        }
    }
    else if (sub == "DEAL") {
        if (!game_running)    return;
        if (hand_in_progress) return;
        int able = 0;
        for (auto& [id, p] : players) if (p.chips > 0) able++;
        if (able < 2) {
            log("[poker] not enough players with chips; auto-rotation stopped.");
            game_running = false;
            return;
        }
        start_hand_locked();
    }
    else if (sub == "REBUY") {
        auto it = players.find(m.id);
        if (it == players.end()) { log("[poker] you are not at the table"); return; }
        if (hand_in_progress)     { log("[poker] cannot rebuy mid-hand"); return; }
        if (it->second.chips > 0) { log("[poker] you still have chips"); return; }
        int amt = DEFAULT_CHIPS;
        if (!payload.empty()) {
            int parsed = std::atoi(payload.c_str());
            if (parsed > 0) amt = parsed;
        }
        it->second.chips = amt;
        it->second.chips_at_hand_start = amt;
        log("*** " + m.name + " rebought for " + std::to_string(amt) +
            " chips. ***");
    }
    else if (sub == "COMMIT") {
        auto it = players.find(m.id);
        if (it == players.end()) return;
        it->second.commit_hash = payload;
        if (!hand_in_progress || !waiting_for_reveals) return;
        log("[poker] " + m.name + " committed");
        maybe_ask_reveal_locked();
    }
    else if (sub == "REVEAL") {
        auto it = players.find(m.id);
        if (it == players.end()) return;
        if (it->second.commit_hash.empty()) return;
        Id check = hash_id(payload);
        if (id_hex(check) != it->second.commit_hash) {
            log("[poker] !! " + m.name + " reveal doesn't match commit");
            return;
        }
        it->second.reveal_seed = payload;
        it->second.revealed = true;
        log("[poker] " + m.name + " revealed seed");
        maybe_finalize_shuffle_locked();
    }
    else if (sub == "ACTION")  handle_action_locked(m, payload);
    else if (sub == "SHOWDOWN") {
        parse_showdown_locked(payload);
    }
}

void PokerGame::handle_private_locked(const Message& m) {
    std::string sub = m.text;
    std::string payload;
    size_t bar = sub.find('|');
    if (bar != std::string::npos) { payload = sub.substr(bar + 1); sub = sub.substr(0, bar); }
    if (sub != "HOLE") return;
    auto it = players.find(self_id);
    if (it == players.end()) return;
    size_t semi = payload.find(';');
    if (semi == std::string::npos) return;
    auto parse_card = [](const std::string& s) {
        size_t c = s.find(',');
        Card card;
        if (c != std::string::npos) {
            card.rank = (uint8_t)std::atoi(s.substr(0, c).c_str());
            card.suit = (uint8_t)std::atoi(s.substr(c + 1).c_str());
            card.valid = true;
        }
        return card;
    };
    it->second.hole[0] = parse_card(payload.substr(0, semi));
    it->second.hole[1] = parse_card(payload.substr(semi + 1));
}

// ---- Commit / reveal flow ----

void PokerGame::maybe_ask_reveal_locked() {
    for (auto& [id, p] : players) {
        if (!p.in_hand) continue;
        if (p.commit_hash.empty()) return;
    }
    if (!my_pending_seed_.empty()) {
        std::string seed = my_pending_seed_;
        my_pending_seed_.clear();
        cb->send_poker(self_name, "REVEAL|" + seed);
    }
}

void PokerGame::maybe_finalize_shuffle_locked() {
    if (!waiting_for_reveals) return;
    for (auto& [id, p] : players) {
        if (!p.in_hand) continue;
        if (!p.revealed) return;
    }
    waiting_for_reveals = false;

    std::vector<std::string> in_hand_order;
    for (auto& id : seat_order_locked())
        if (players[id].in_hand) in_hand_order.push_back(id);

    std::vector<std::string> seeds;
    for (auto& id : in_hand_order) seeds.push_back(players[id].reveal_seed);

    {
        unsigned char h[SHA256_DIGEST_LENGTH];
        std::string joined;
        for (auto& s : seeds) { joined += s; joined += "|"; }
        SHA256(reinterpret_cast<const unsigned char*>(joined.data()),
               joined.size(), h);
        combined_seed = hex_encode(h, 8);
    }

    deck = shuffled_deck(seeds);
    deck_pos = 0;

    size_t n = in_hand_order.size();
    auto it_d = std::find(in_hand_order.begin(), in_hand_order.end(), dealer_id);
    size_t dealerIdx = (it_d == in_hand_order.end()) ? 0 : (size_t)(it_d - in_hand_order.begin());

    for (size_t k = 0; k < n; ++k) {
        size_t idx = (dealerIdx + 1 + k) % n;
        players[in_hand_order[idx]].hole[0] = deck[deck_pos++];
    }
    for (size_t k = 0; k < n; ++k) {
        size_t idx = (dealerIdx + 1 + k) % n;
        players[in_hand_order[idx]].hole[1] = deck[deck_pos++];
    }

    for (auto& [id, p] : players) {
        if (!p.in_hand) continue;
        if (id == self_id) continue;
        std::string cards;
        cards += std::to_string(p.hole[0].rank) + "," + std::to_string(p.hole[0].suit);
        cards += ";";
        cards += std::to_string(p.hole[1].rank) + "," + std::to_string(p.hole[1].suit);
        cb->send_private(self_name, id, "HOLE|" + cards);
    }

    if (n == 2) {
        sb_id = dealer_id;
        bb_id = next_active_locked(dealer_id);
    } else {
        sb_id = next_active_locked(dealer_id);
        bb_id = next_active_locked(sb_id);
    }
    if (!sb_id.empty()) post_blind_locked(sb_id, small_blind);
    if (!bb_id.empty()) post_blind_locked(bb_id, big_blind);
    current_bet = big_blind;
    if (!sb_id.empty()) players[sb_id].has_acted_this_street = false;
    if (!bb_id.empty()) players[bb_id].has_acted_this_street = false;

    turn_id = next_active_locked(bb_id);
    if (turn_id.empty()) turn_id = sb_id;

    phase = PokerPhase::PREFLOP;
    log("");
    log("=== HAND #" + std::to_string(hand_number) +
        " === Dealer: " + players[dealer_id].name +
        "   Blinds " + std::to_string(small_blind) + "/" +
        std::to_string(big_blind));
    announce_turn_locked();
}

// ---- Betting mechanics ----

void PokerGame::post_blind_locked(const std::string& id, int amt) {
    auto& p = players[id];
    int a = std::min(amt, p.chips);
    p.chips -= a;
    p.committed_this_street += a;
    p.committed_total += a;
    pot += a;
    if (p.chips == 0) p.all_in = true;
}

void PokerGame::announce_turn_locked() {
    if (turn_id.empty()) return;
    auto it = players.find(turn_id);
    if (it == players.end()) return;
    int owe = std::max(0, current_bet - it->second.committed_this_street);

    if (turn_id == self_id) {
        log("");
        log(render_box_locked());
        log("Your turn. To call: " + std::to_string(owe) +
            ".  [f]old  [c]heck/call  [r]aise N");
    } else {
        log("[poker] " + it->second.name + " to act" +
            (owe > 0 ? " (owes " + std::to_string(owe) + ")" : ""));
    }
}

void PokerGame::handle_action_locked(const Message& m, const std::string& payload) {
    if (!hand_in_progress) return;
    if (m.id != turn_id) return;
    auto it = players.find(m.id);
    if (it == players.end() || !it->second.in_hand || it->second.folded) return;
    auto& p = it->second;

    std::string verb; int amount = 0;
    size_t sp = payload.find(' ');
    if (sp == std::string::npos) verb = payload;
    else { verb = payload.substr(0, sp); amount = std::atoi(payload.substr(sp + 1).c_str()); }

    if (verb == "fold") {
        p.folded = true;
        log(p.name + " folds.");
    }
    else if (verb == "check") {
        if (p.committed_this_street < current_bet) {
            log("[poker] " + p.name + " cannot check; owes " +
                std::to_string(current_bet - p.committed_this_street));
            return;
        }
        log(p.name + " checks.");
    }
    else if (verb == "call") {
        int owe = current_bet - p.committed_this_street;
        int a = std::min(owe, p.chips);
        p.chips -= a; p.committed_this_street += a;
        p.committed_total += a; pot += a;
        if (p.chips == 0) p.all_in = true;
        if (a == 0) log(p.name + " checks.");
        else        log(p.name + " calls " + std::to_string(a) +
                       (p.all_in ? " (all-in)" : ""));
    }
    else if (verb == "bet" || verb == "raise") {
        if (amount <= current_bet) {
            log("[poker] " + verb + " must exceed current bet of " +
                std::to_string(current_bet) + " (you sent " +
                std::to_string(amount) + ")");
            return;
        }
        if (amount <= p.committed_this_street) {
            log("[poker] " + verb + " amount must exceed your own street "
                "commitment of " + std::to_string(p.committed_this_street));
            return;
        }
        int extra = amount - p.committed_this_street;
        int a = std::min(extra, p.chips);
        p.chips -= a;
        p.committed_this_street += a;
        p.committed_total += a;
        pot += a;
        if (p.committed_this_street > current_bet)
            current_bet = p.committed_this_street;
        if (p.chips == 0) p.all_in = true;
        log(p.name + " " + verb + "s to " + std::to_string(p.committed_this_street) +
            (p.all_in ? " (all-in)" : ""));
        for (auto& [id, q] : players)
            if (q.in_hand && !q.folded && !q.all_in && id != p.id)
                q.has_acted_this_street = false;
    }
    else { log("[poker] unknown action: " + verb); return; }

    p.has_acted_this_street = true;
    advance_if_needed_locked();
}

void PokerGame::advance_if_needed_locked() {
    std::vector<std::string> active;
    for (auto& [id, p] : players)
        if (p.in_hand && !p.folded) active.push_back(id);

    if (active.size() <= 1) {
        if (active.size() == 1) {
            auto& w = players[active[0]];
            w.chips += pot;
            log("*** " + w.name + " wins pot " + std::to_string(pot) +
                " (all others folded) ***");
            pot = 0;
        }
        end_hand_locked();
        return;
    }

    bool all_done = true;
    for (auto& id : active) {
        auto& p = players[id];
        if (p.all_in) continue;
        if (!p.has_acted_this_street) { all_done = false; break; }
        if (p.committed_this_street < current_bet) { all_done = false; break; }
    }
    if (all_done) { advance_street_locked(); return; }

    std::string nxt = next_active_locked(turn_id);
    if (nxt.empty() || nxt == turn_id) { advance_street_locked(); return; }
    turn_id = nxt;
    announce_turn_locked();
}

void PokerGame::advance_street_locked() {
    for (auto& [id, p] : players) {
        p.committed_this_street = 0;
        p.has_acted_this_street = false;
    }
    current_bet = 0;

    if (phase == PokerPhase::PREFLOP) {
        deck_pos++;
        community.push_back(deck[deck_pos++]);
        community.push_back(deck[deck_pos++]);
        community.push_back(deck[deck_pos++]);
        phase = PokerPhase::FLOP;
        log("-- FLOP --  " + board_string_locked());
    } else if (phase == PokerPhase::FLOP) {
        deck_pos++;
        community.push_back(deck[deck_pos++]);
        phase = PokerPhase::TURN;
        log("-- TURN --  " + board_string_locked());
    } else if (phase == PokerPhase::TURN) {
        deck_pos++;
        community.push_back(deck[deck_pos++]);
        phase = PokerPhase::RIVER;
        log("-- RIVER --  " + board_string_locked());
    } else if (phase == PokerPhase::RIVER) {
        do_showdown_locked();
        return;
    }

    std::string first = next_active_locked(dealer_id);
    while (!first.empty() && players[first].all_in)
        first = next_active_locked(first);
    if (first.empty()) { advance_street_locked(); return; }
    turn_id = first;
    announce_turn_locked();
}

std::string PokerGame::board_string_locked() const {
    std::string s;
    for (auto& c : community) s += c.ansi() + " ";
    return s;
}

// ---- Showdown ----

void PokerGame::do_showdown_locked() {
    log("-- SHOWDOWN --");
    std::string payload;
    bool first = true;
    for (auto& [id, p] : players) {
        if (!p.in_hand || p.folded) continue;
        if (!first) payload += ";";
        payload += id + ":";
        payload += std::to_string(p.hole[0].rank) + "," +
                   std::to_string(p.hole[0].suit) + "," +
                   std::to_string(p.hole[1].rank) + "," +
                   std::to_string(p.hole[1].suit);
        first = false;
    }
    phase = PokerPhase::SHOWDOWN;
    cb->send_poker(self_name, "SHOWDOWN|" + payload);
}

void PokerGame::parse_showdown_locked(const std::string& payload) {
    if (showdown_processed) return;
    showdown_processed = true;

    std::map<std::string, std::array<Card, 2>> shown;
    std::stringstream ss(payload);
    std::string item;
    while (std::getline(ss, item, ';')) {
        size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        std::string pid = item.substr(0, colon);
        std::string rest = item.substr(colon + 1);
        std::vector<int> nums;
        std::stringstream ns(rest);
        std::string tok;
        while (std::getline(ns, tok, ',')) nums.push_back(std::atoi(tok.c_str()));
        if (nums.size() != 4) continue;
        std::array<Card, 2> cards;
        cards[0].rank = (uint8_t)nums[0]; cards[0].suit = (uint8_t)nums[1]; cards[0].valid = true;
        cards[1].rank = (uint8_t)nums[2]; cards[1].suit = (uint8_t)nums[3]; cards[1].valid = true;
        shown[pid] = cards;
        auto it = players.find(pid);
        if (it != players.end()) {
            it->second.hole[0] = cards[0];
            it->second.hole[1] = cards[1];
            it->second.shown = true;
        }
    }

    HandValue best;
    std::vector<std::string> winners;
    for (auto& [pid, cards] : shown) {
        std::vector<Card> all = community;
        all.push_back(cards[0]);
        all.push_back(cards[1]);
        HandValue hv = evaluate_7(all);
        log(players[pid].name + " shows " +
            cards[0].ansi() + " " + cards[1].ansi() + " -> " + hv.str());
        if (winners.empty() || best < hv) {
            best = hv;
            winners.clear();
            winners.push_back(pid);
        } else if (hv == best) {
            winners.push_back(pid);
        }
    }
    if (winners.empty()) {
        log("[poker] no showdown data -- pot refunded");
        for (auto& [id, p] : players) {
            if (p.in_hand && !p.folded) { p.chips += pot; break; }
        }
        pot = 0;
        end_hand_locked();
        return;
    }
    int share = pot / (int)winners.size();
    int rem = pot - share * (int)winners.size();
    std::string names;
    for (size_t i = 0; i < winners.size(); ++i) {
        players[winners[i]].chips += share + (i == 0 ? rem : 0);
        if (i) names += ", ";
        names += players[winners[i]].name;
    }
    log("*** " + names + " wins pot " + std::to_string(pot) +
        " with " + best.str() + " ***");
    pot = 0;
    end_hand_locked();
}

// ---- Hand lifecycle ----

void PokerGame::end_hand_locked() {
    hand_in_progress = false;
    phase = PokerPhase::IDLE;
    waiting_for_reveals = false;
    my_pending_seed_.clear();

    std::ostringstream dl;
    dl << "[stacks] ";
    for (auto& [id, p] : players) {
        int d = p.chips - p.chips_at_hand_start;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s=%d (%+d)",
                      p.name.c_str(), p.chips, d);
        dl << buf << "  ";
    }
    log(dl.str());

    for (auto& [id, p] : players) {
        p.in_hand = false;
        p.folded = false;
        p.all_in = false;
        p.committed_this_street = 0;
        p.committed_total = 0;
        p.has_acted_this_street = false;
        p.commit_hash.clear();
        p.reveal_seed.clear();
        p.revealed = false;
        p.shown = false;
        p.hole[0] = Card{};
        p.hole[1] = Card{};
    }
    turn_id.clear();
    sb_id.clear();
    bb_id.clear();

    for (auto& [id, p] : players) {
        if (p.chips <= 0) {
            log("*** " + p.name + " is out of chips. Use /poker rebuy. ***");
        }
    }

    if (game_running) {
        if (self_id == game_leader_id) {
            log("[poker] Next hand in " +
                std::to_string(NEXT_HAND_DELAY_SEC) + "s...");
            auto cb_local   = cb;
            auto name_local = self_name;
            std::thread([cb_local, name_local]() {
                std::this_thread::sleep_for(
                    std::chrono::seconds(NEXT_HAND_DELAY_SEC));
                cb_local->send_poker(name_local, "DEAL|");
            }).detach();
        } else {
            log("[poker] Waiting for next hand...");
        }
    } else {
        log("[poker] Type /poker start (auto-rotate) or /poker deal "
            "(one hand) to continue.");
    }
}

void PokerGame::start_hand_locked() {
    int able = 0;
    for (auto& [id, p] : players) if (p.chips > 0) able++;
    if (able < 2) {
        log("[poker] need at least 2 players with chips to start a hand");
        game_running = false;
        return;
    }

    hand_in_progress = true;
    hand_number++;
    phase = PokerPhase::PREFLOP;
    pot = 0;
    current_bet = 0;
    community.clear();
    waiting_for_reveals = true;
    showdown_processed = false;
    combined_seed.clear();

    for (auto& [id, p] : players) {
        p.hole[0] = Card{};
        p.hole[1] = Card{};
        p.folded = false;
        p.all_in = false;
        p.committed_this_street = 0;
        p.committed_total = 0;
        p.has_acted_this_street = false;
        p.commit_hash.clear();
        p.reveal_seed.clear();
        p.revealed = false;
        p.shown = false;
        p.in_hand = (p.chips > 0);
        p.chips_at_hand_start = p.chips;
    }

    std::vector<std::string> order;
    for (auto& id : seat_order_locked())
        if (players[id].in_hand) order.push_back(id);

    if (dealer_id.empty() || !players.count(dealer_id)
        || !players[dealer_id].in_hand) {
        dealer_id = order[0];
    } else {
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == dealer_id) {
                dealer_id = order[(i + 1) % order.size()];
                break;
            }
        }
    }

    my_pending_seed_ = random_seed_hex();
    std::string commit = id_hex(hash_id(my_pending_seed_));
    cb->send_poker(self_name, "COMMIT|" + commit);
}

} // namespace advchat
