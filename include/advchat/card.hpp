#pragma once
#include <cstdint>
#include <string>

namespace advchat {

struct Card {
    uint8_t rank = 0;
    uint8_t suit = 0;
    bool    valid = false;

    std::string rank_str() const;
    std::string rank_padded() const;
    std::string suit_str() const;
    std::string str() const;
    std::string ansi() const;
};

} // namespace advchat
