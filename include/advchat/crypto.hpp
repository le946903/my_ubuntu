#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace advchat {

using Id = std::array<uint8_t, 32>;

Id          hash_id(const std::string& s);
std::string id_hex(const Id& id);

class Sha256CtrRng {
public:
    explicit Sha256CtrRng(const std::string& seed);

    uint32_t next_u32();
    uint64_t next_u64();
    uint32_t uniform(uint32_t lo, uint32_t hi);
    uint32_t rank_2_to_14();
    uint32_t roll_1_to_14();

private:
    static constexpr size_t kWordsPerBlock = 8;
    void refill();

    std::string seed_;
    uint64_t    counter_;
    uint32_t    words_[kWordsPerBlock];
    size_t      pos_;
};

} // namespace advchat
