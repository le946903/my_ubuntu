#include "advchat/vector_clock.hpp"

#include <sstream>

namespace advchat {

void VectorClock::inc(const std::string& self) { v[self]++; }

std::string VectorClock::serialize() const {
    std::ostringstream os;
    bool first = true;
    for (auto& [k, c] : v) {
        if (!first) os << ",";
        os << k << ":" << c;
        first = false;
    }
    return os.str();
}

VectorClock VectorClock::deserialize(const std::string& s) {
    VectorClock vc;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string item = s.substr(pos, (comma == std::string::npos)
                                          ? std::string::npos : comma - pos);
        size_t colon = item.find(':');
        if (colon != std::string::npos) {
            std::string k = item.substr(0, colon);
            uint64_t c = 0;
            try { c = std::stoull(item.substr(colon + 1)); } catch (...) {}
            if (!k.empty()) vc.v[k] = c;
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return vc;
}

} // namespace advchat
