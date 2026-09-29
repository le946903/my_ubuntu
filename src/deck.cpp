#include "advchat/deck.hpp"

#include <algorithm>

#include "advchat/crypto.hpp"

namespace advchat {

std::vector<Card> shuffled_deck(const std::vector<std::string>& seeds) {
    std::vector<std::string> s = seeds;
    std::sort(s.begin(), s.end());

    std::string combined;
    for (auto& x : s) { combined += x; combined += "|"; }

    Sha256CtrRng rng(combined);

    std::vector<Card> deck;
    deck.reserve(52);
    for (uint8_t su = 0; su < 4; ++su)
        for (uint8_t r = 2; r <= 14; ++r) {
            Card c; c.rank = r; c.suit = su; c.valid = true;
            deck.push_back(c);
        }

    for (int i = (int)deck.size() - 1; i > 0; --i) {
        uint32_t j = rng.uniform(0, (uint32_t)i);
        std::swap(deck[i], deck[j]);
    }
    return deck;
}

} // namespace advchat
