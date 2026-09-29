#include "advchat/app.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "advchat/causal_broadcast.hpp"
#include "advchat/config.hpp"
#include "advchat/crypto.hpp"
#include "advchat/helpers.hpp"
#include "advchat/net_io.hpp"
#include "advchat/poker_game.hpp"

namespace advchat {

static int run_impl(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <your_name>\n";
        return 1;
    }
    g_my_name   = argv[1];
    g_my_id     = hash_id(g_my_name);
    g_my_id_hex = id_hex(g_my_id);

    {
        std::string logname = "poker_debug_" + g_my_name + ".log";
        g_debug_log.open(logname, std::ios::out | std::ios::trunc);
        if (g_debug_log.is_open())
            dlog("=== session start name=", g_my_name,
                 " id=", g_my_id_hex.substr(0, 16), " ===");
    }

    int listen_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in laddr{};
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);
    laddr.sin_port = 0;
    if (::bind(listen_sock, (sockaddr*)&laddr, sizeof(laddr)) < 0) {
        perror("bind"); return 1;
    }
    if (::listen(listen_sock, 16) < 0) { perror("listen"); return 1; }

    sockaddr_in actual{};
    socklen_t alen = sizeof(actual);
    getsockname(listen_sock, (sockaddr*)&actual, &alen);
    g_my_tcp_port = ntohs(actual.sin_port);

    PokerGame poker;
    poker.self_id = g_my_id_hex;
    poker.self_name = g_my_name;

    auto deliver_fn = [&poker](const Message& m) {
        if (m.type == MsgType::CHAT) {
            dlog("DELIVER chat from=", m.name, " text=", m.text);
            say("\r[", now_hhmmss(), "] ", m.name, " (L=", m.lamport, "): ",
                m.text, "\n> ");
        } else if (m.type == MsgType::LEAVE) {
            dlog("DELIVER leave from=", m.name);
            say("\r[", now_hhmmss(), "] *** ", m.name, " left the room ***\n> ");
        } else if (m.type == MsgType::POKER || m.type == MsgType::PRIV) {
            dlog("DELIVER ", msg_type_str(m.type), " from=", m.name,
                 " text=", m.text);
            poker.handle_message(m);
        }
    };
    auto send_fn = [](const Message& m) { net_broadcast(m); };

    CausalBroadcast cb(g_my_id_hex, deliver_fn, send_fn);
    g_cb = &cb;
    poker.cb = &cb;
    poker.log = [](const std::string& s) {
        dlog("POKER ", s);
        say("\r", s, "\n> ");
    };

    say("=== Advanced P2P LAN Chat + Texas Hold'em ===\n");
    say("Name     : ", g_my_name, "\n");
    say("ID       : ", g_my_id_hex.substr(0, 16), "...\n");
    say("TCP port : ", g_my_tcp_port, "\n");
    say("Multicast: ", MULTICAST_ADDR, ":", MULTICAST_PORT, "\n");
    say("Chat commands : /peers  /quit\n");
    say("Poker commands: /poker join | /poker leave | /poker rebuy\n");
    say("                /poker start (auto-rotate) | /poker stop\n");
    say("                /poker deal  (single hand) | /poker state\n");
    say("Actions       : f | c | r N   (fold / check-call / raise to N)\n");
    say("                fold | check | call | bet N | raise N\n");
    say("Debug log: poker_debug_", g_my_name, ".log\n");
    say("Type a message or command and press Enter.\n> ");

    int udp_sock = setup_multicast_socket();
    if (udp_sock < 0) return 1;

    std::thread t_accept  (tcp_accept_loop, listen_sock);
    std::thread t_announce(udp_announce_loop, udp_sock);
    std::thread t_ulisten (udp_listen_loop, udp_sock);
    std::thread t_reaper  (reaper_loop);

    std::string line;
    bool quitting = false;
    while (g_running && std::getline(std::cin, line)) {
        if (line == "/quit" || line == "/exit") { quitting = true; break; }
        if (line == "/peers") {
            std::lock_guard<std::mutex> lk(g_peers_mu);
            say("Peers (", peers().size(), "):\n");
            for (auto& [id, p] : peers()) {
                say("  - ", p.name, " [", id.substr(0, 8), "...]",
                    " @ ", p.ip, ":", p.port,
                    (p.sock >= 0 ? " [connected]" : " [disconnected]"), "\n");
            }
            say("> ");
            continue;
        }

        if (line.rfind("/poker ", 0) == 0) {
            std::string sub = line.substr(7);
            if (sub == "join") {
                cb.send_poker(g_my_name, "JOIN|");
            } else if (sub == "leave") {
                cb.send_poker(g_my_name, "LEAVE_TABLE|");
            } else if (sub == "rebuy") {
                cb.send_poker(g_my_name, "REBUY|" + std::to_string(DEFAULT_CHIPS));
            } else if (sub == "start") {
                std::lock_guard<std::recursive_mutex> lk(poker.pmu_);
                if (poker.players.size() < 2) {
                    say("[poker] need at least 2 players\n> ");
                } else {
                    cb.send_poker(g_my_name, "START|");
                }
            } else if (sub == "stop") {
                cb.send_poker(g_my_name, "STOP|");
            } else if (sub == "deal") {
                cb.send_poker(g_my_name, "DEAL|");
            } else if (sub == "state") {
                std::lock_guard<std::recursive_mutex> lk(poker.pmu_);
                if (poker.players.empty()) {
                    say("[poker] no players at the table\n> ");
                } else {
                    say("\n");
                    say(poker.render_box_locked());
                    say("> ");
                }
            } else {
                say("[poker] unknown subcommand. Use: join|leave|rebuy|start|"
                    "stop|deal|state\n> ");
            }
            continue;
        }

        bool is_poker_action = false;
        std::string action_line;

        if (line == "fold")      { is_poker_action = true; action_line = "ACTION|fold"; }
        else if (line == "check"){ is_poker_action = true; action_line = "ACTION|check"; }
        else if (line == "call") { is_poker_action = true; action_line = "ACTION|call"; }
        else if (line.rfind("bet ", 0) == 0 ||
                 line.rfind("raise ", 0) == 0) {
            is_poker_action = true;
            action_line = "ACTION|" + line;
        }
        else if (line == "f")     { is_poker_action = true; action_line = "ACTION|fold"; }
        else if (line == "c")     { is_poker_action = true; action_line = "ACTION|call"; }
        else if (line.rfind("r ", 0) == 0) {
            is_poker_action = true;
            action_line = "ACTION|raise " + line.substr(2);
        }
        else if (line.rfind("b ", 0) == 0) {
            is_poker_action = true;
            action_line = "ACTION|bet " + line.substr(2);
        }

        if (is_poker_action) {
            bool in_hand;
            bool my_turn;
            {
                std::lock_guard<std::recursive_mutex> lk(poker.pmu_);
                in_hand  = poker.hand_in_progress;
                my_turn  = (poker.turn_id == g_my_id_hex);
            }
            if (!in_hand) {
                say("[poker] no hand in progress -- sending as chat\n> ");
                cb.send_local(g_my_name, line);
            } else if (!my_turn) {
                say("[poker] not your turn\n> ");
            } else {
                dlog("INPUT action ", action_line);
                cb.send_poker(g_my_name, action_line);
            }
            continue;
        }

        if (line.empty()) { say("> "); continue; }
        dlog("INPUT chat ", line);
        cb.send_local(g_my_name, line);
    }

    if (quitting) {
        say("\r[*] Broadcasting LEAVE...\n");
        {
            std::lock_guard<std::recursive_mutex> lk(poker.pmu_);
            if (!poker.players.empty())
                cb.send_poker(g_my_name, "LEAVE_TABLE|");
        }
        cb.send_leave(g_my_name);
        std::this_thread::sleep_for(std::chrono::milliseconds(LEAVE_FLUSH_MS));
    }

    g_running = false;
    ::shutdown(listen_sock, SHUT_RDWR);
    ::close(listen_sock);
    ::close(udp_sock);

    t_accept.join();
    t_announce.join();
    t_ulisten.join();
    t_reaper.join();

    dlog("=== session end ===");
    if (g_debug_log.is_open()) g_debug_log.close();
    say("\nBye.\n");
    return 0;
}

int run(int argc, char** argv) { return run_impl(argc, argv); }

} // namespace advchat
