#include "advchat/hand_eval.hpp"

#include <algorithm>
#include <functional>

namespace advchat {

bool HandValue::operator<(const HandValue& o) const {
    if (rank != o.rank) return rank < o.rank;
    return kickers < o.kickers;
}

bool HandValue::operator==(const HandValue& o) const {
    return rank == o.rank && kickers == o.kickers;
}

std::string HandValue::str() const {
    static const char* N[] = {
        "High Card", "Pair", "Two Pair", "Trips",
        "Straight", "Flush", "Full House", "Quads",
        "Straight Flush", "Royal Flush"
    };
    return N[(int)rank];
}

HandValue evaluate_7(const std::vector<Card>& cards) {
    HandValue best;
    best.rank = HandRank::HIGH_CARD;
    best.kickers = {0,0,0,0,0};
    int n = (int)cards.size();
    if (n < 5) return best;

    std::vector<int> idx(5);
    std::function<void(int,int)> rec = [&](int start, int depth) {
        if (depth == 5) {
            int cnt[15] = {0};
            int suitCnt[4] = {0};
            for (int i = 0; i < 5; ++i) {
                Card c = cards[idx[i]];
                cnt[c.rank]++;
                suitCnt[c.suit]++;
            }
            bool flush = false;
            for (int s = 0; s < 4; ++s) if (suitCnt[s] == 5) flush = true;

            int straightHigh = 0;
            for (int hi = 14; hi >= 6; --hi) {
                bool ok = true;
                for (int k = 0; k < 5; ++k) if (!cnt[hi - k]) { ok = false; break; }
                if (ok) { straightHigh = hi; break; }
            }
            if (!straightHigh && cnt[14] && cnt[2] && cnt[3] && cnt[4] && cnt[5])
                straightHigh = 5;

            std::vector<std::pair<int,int>> groups;
            for (int r = 14; r >= 2; --r) if (cnt[r]) groups.push_back({cnt[r], r});
            std::sort(groups.begin(), groups.end(),
                [](const std::pair<int,int>& a, const std::pair<int,int>& b) {
                    if (a.first != b.first) return a.first > b.first;
                    return a.second > b.second;
                });

            HandValue hv;
            hv.kickers = {0,0,0,0,0};
            if (flush && straightHigh == 14) hv.rank = HandRank::ROYAL_FLUSH;
            else if (flush && straightHigh)  hv.rank = HandRank::STRAIGHT_FLUSH;
            else if (!flush && straightHigh) hv.rank = HandRank::STRAIGHT;
            else if (flush)                  hv.rank = HandRank::FLUSH;
            else {
                if (groups[0].first == 4) hv.rank = HandRank::QUADS;
                else if (groups[0].first == 3 && groups.size() > 1 && groups[1].first == 2)
                    hv.rank = HandRank::FULL_HOUSE;
                else if (groups[0].first == 3) hv.rank = HandRank::TRIPS;
                else if (groups[0].first == 2 && groups.size() > 1 && groups[1].first == 2)
                    hv.rank = HandRank::TWO_PAIR;
                else if (groups[0].first == 2) hv.rank = HandRank::PAIR;
                else hv.rank = HandRank::HIGH_CARD;
            }

            std::array<uint8_t, 5> k{}; int ki = 0;
            if (hv.rank == HandRank::STRAIGHT ||
                hv.rank == HandRank::STRAIGHT_FLUSH ||
                hv.rank == HandRank::ROYAL_FLUSH) {
                k[0] = (uint8_t)straightHigh;
            } else if (hv.rank == HandRank::FLUSH || hv.rank == HandRank::HIGH_CARD) {
                std::vector<int> rs;
                for (int r = 14; r >= 2; --r) if (cnt[r]) rs.push_back(r);
                for (int i = 0; i < 5 && i < (int)rs.size(); ++i) k[i] = (uint8_t)rs[i];
            } else {
                for (auto& g : groups)
                    for (int c = 0; c < g.first && ki < 5; ++c) k[ki++] = (uint8_t)g.second;
            }
            hv.kickers = k;
            if (best < hv) best = hv;
            return;
        }
        for (int i = start; i < n; ++i) { idx[depth] = i; rec(i + 1, depth + 1); }
    };
    rec(0, 0);
    return best;
}

} // namespace advchat
