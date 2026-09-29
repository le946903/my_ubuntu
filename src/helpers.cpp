#include "advchat/helpers.hpp"

#include <chrono>
#include <cstring>
#include <ctime>

namespace advchat {

std::mutex    g_cout_mu;
std::mutex    g_log_mu;
std::ofstream g_debug_log;

std::string now_hhmmss() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return buf;
}

std::string hex_encode(const unsigned char* p, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += h[p[i] >> 4];
        s += h[p[i] & 0xF];
    }
    return s;
}

size_t vis_len(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '\033') {
            size_t j = i + 1;
            while (j < s.size() && s[j] != 'm') ++j;
            i = (j < s.size()) ? j + 1 : s.size();
        } else if ((unsigned char)s[i] >= 0x80) {
            unsigned char c = (unsigned char)s[i];
            if      ((c & 0xE0) == 0xC0) i += 2;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else if ((c & 0xF8) == 0xF0) i += 4;
            else                         i += 1;
            ++n;
        } else {
            ++i;
            ++n;
        }
    }
    return n;
}

std::string pad_to(const std::string& s, size_t width) {
    size_t v = vis_len(s);
    if (v >= width) return s;
    return s + std::string(width - v, ' ');
}

} // namespace advchat
