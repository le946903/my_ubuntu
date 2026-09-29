#pragma once
#include <iostream>
#include <mutex>
#include <fstream>

namespace advchat {

extern std::mutex  g_cout_mu;
extern std::mutex  g_log_mu;
extern std::ofstream g_debug_log;

template <typename... Ts>
void say(Ts&&... ts) {
    std::lock_guard<std::mutex> lk(g_cout_mu);
    (std::cout << ... << ts) << std::flush;
}

template <typename... Ts>
void dlog(Ts&&... ts) {
    std::lock_guard<std::mutex> lk(g_log_mu);
    if (!g_debug_log.is_open()) return;
    g_debug_log << '[' << now_hhmmss() << "] ";
    (g_debug_log << ... << ts);
    g_debug_log << '\n';
    g_debug_log.flush();
}

} // namespace advchat
