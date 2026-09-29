#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "advchat/card.hpp"

namespace advchat {

enum class HandRank {
    HIGH_CARD, PAIR, TWO_PAIR, TRIPS, STRAIGHT,
    FLUSH, FULL_HOUSE, QUADS, STRAIGHT_FLUSH, ROYAL_FLUSH
};

struct HandValue {
    HandRank rank = HandRank::HIGH_CARD;
    std::array<uint8_t, 5> kickers{};

    bool operator<(const HandValue& o) const;
    bool operator==(const HandValue& o) const;
    std::string str() const;
};

HandValue evaluate_7(const std::vector<Card>& cards);

} // namespace advchat
