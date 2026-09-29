#pragma once
#include <string>
#include <vector>

#include "advchat/card.hpp"

namespace advchat {

std::vector<Card> shuffled_deck(const std::vector<std::string>& seeds);

} // namespace advchat
