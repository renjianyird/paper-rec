cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(paper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB SOURCES src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    include/npcap
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ws2_32
    wininet
    iphlpapi
    urlmon
    wpcap
    Packet
)

target_link_options(${PROJECT_NAME} PRIVATE
    -static-libgcc
    -static-libstdc++
)
