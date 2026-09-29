#pragma once
#include <string>

namespace advchat {

std::string now_hhmmss();

template <typename... Ts>
void say(Ts&&... ts);

template <typename... Ts>
void dlog(Ts&&... ts);

std::string hex_encode(const unsigned char* p, size_t n);

// Visible width (ANSI-aware, UTF-8 aware).
size_t vis_len(const std::string& s);

std::string pad_to(const std::string& s, size_t width);

} // namespace advchat

#include "helpers.tpp"   // template definitions
