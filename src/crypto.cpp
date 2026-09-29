#include "advchat/crypto.hpp"

#include <openssl/sha.h>

#include <algorithm>
#include <vector>

#include "advchat/helpers.hpp"

namespace advchat {

Id hash_id(const std::string& s) {
    Id out{};
    SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), out.data());
    return out;
}

std::string id_hex(const Id& id) {
    return hex_encode(id.data(), id.size());
}

Sha256CtrRng::Sha256CtrRng(const std::string& seed)
    : seed_(seed), counter_(0), pos_(kWordsPerBlock) {}

uint32_t Sha256CtrRng::next_u32() {
    if (pos_ >= kWordsPerBlock) refill();
    return words_[pos_++];
}

uint64_t Sha256CtrRng::next_u64() {
    uint64_t hi = next_u32();
    uint64_t lo = next_u32();
    return (hi << 32) | lo;
}

uint32_t Sha256CtrRng::uniform(uint32_t lo, uint32_t hi) {
    if (hi < lo) std::swap(lo, hi);
    uint64_t range = (uint64_t)hi - lo + 1;
    if (range == 0) return next_u32();
    if (range == 1) return lo;
    uint64_t limit = (uint64_t)UINT32_MAX - ((uint64_t)UINT32_MAX % range);
    uint32_t w;
    do { w = next_u32(); } while ((uint64_t)w >= limit);
    return lo + (uint32_t)(w % range);
}

uint32_t Sha256CtrRng::rank_2_to_14() { return uniform(2, 14); }
uint32_t Sha256CtrRng::roll_1_to_14()  { return uniform(1, 14); }

void Sha256CtrRng::refill() {
    std::vector<unsigned char> buf;
    if (seed_.size() > SHA256_DIGEST_LENGTH) {
        unsigned char h[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(seed_.data()), seed_.size(), h);
        buf.insert(buf.end(), h, h + SHA256_DIGEST_LENGTH);
    } else {
        buf.insert(buf.end(), seed_.begin(), seed_.end());
    }
    buf.push_back(0x00);
    uint64_t ctr = counter_++;
    for (int i = 7; i >= 0; --i)
        buf.push_back((unsigned char)((ctr >> (i * 8)) & 0xFF));

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(buf.data(), buf.size(), digest);

    for (size_t i = 0; i < kWordsPerBlock; ++i) {
        words_[i] = ((uint32_t)digest[i*4    ] << 24)
                  | ((uint32_t)digest[i*4 + 1] << 16)
                  | ((uint32_t)digest[i*4 + 2] <<  8)
                  | ((uint32_t)digest[i*4 + 3]);
    }
    pos_ = 0;
}

} // namespace advchat
