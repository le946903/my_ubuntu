#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace advchat {

struct VectorClock {
    std::map<std::string, uint64_t> v;

    void inc(const std::string& self);
    std::string serialize() const;
    static VectorClock deserialize(const std::string& s);
};

} // namespace advchat
