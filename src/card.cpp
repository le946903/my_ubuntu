#include "advchat/card.hpp"

namespace advchat {

std::string Card::rank_str() const {
    if (!valid) return "?";
    switch (rank) {
        case 10: return "10";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        case 14: return "A";
        default: return std::string(1, char('0' + rank));
    }
}

std::string Card::rank_padded() const {
    std::string r = rank_str();
    while (r.size() < 2) r = " " + r;
    return r;
}

std::string Card::suit_str() const {
    switch (suit) {
        case 0: return "\u2660";
        case 1: return "\u2665";
        case 2: return "\u2666";
        case 3: return "\u2663";
        default: return "?";
    }
}

std::string Card::str() const {
    if (!valid) return "\u2591\u2591";
    return rank_padded() + suit_str();
}

std::string Card::ansi() const {
    if (!valid) return "\033[90m\u2591\u2591\033[0m";
    const char* color = (suit == 1 || suit == 2) ? "\033[1;31m" : "\033[1;37m";
    return std::string(color) + str() + "\033[0m";
}

} // namespace advchat
