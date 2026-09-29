cmake_minimum_required(VERSION 3.16)
project(advanced_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(new_advanced_chat
    src/main.cpp
    src/app.cpp
    src/helpers.cpp
    src/crypto.cpp
    src/vector_clock.cpp
    src/card.cpp
    src/hand_eval.cpp
    src/deck.cpp
    src/message.cpp
    src/causal_broadcast.cpp
    src/net_io.cpp
    src/peer.cpp
    src/poker_game.cpp
)

target_include_directories(new_advanced_chat PRIVATE include)
target_link_libraries(new_advanced_chat PRIVATE
    Threads::Threads
    OpenSSL::Crypto
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(new_advanced_chat PRIVATE -O2 -Wall -Wextra)
endif()
