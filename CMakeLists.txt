cmake_minimum_required(VERSION 3.20)
project(nasdaq_itch_handler VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
    -Wall -Wextra -Wpedantic
    -O3
    -march=native
    -funroll-loops
    -fno-omit-frame-pointer
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-falign-functions=64 -falign-loops=64)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(orderbook STATIC
    src/book/order_book.cpp
)
target_include_directories(orderbook PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(netlib STATIC
    src/net/udp_multicast.cpp
)
target_include_directories(netlib PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(feed_handler src/main.cpp)
target_link_libraries(feed_handler PRIVATE orderbook netlib)

find_package(Threads REQUIRED)
target_link_libraries(feed_handler PRIVATE Threads::Threads)

add_executable(bench bench/bench_main.cpp)
target_link_libraries(bench PRIVATE orderbook Threads::Threads)

install(TARGETS feed_handler bench RUNTIME DESTINATION bin)
