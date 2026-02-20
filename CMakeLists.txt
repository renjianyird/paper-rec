cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(paper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 只开基础优化，不卡编译
add_compile_options(-O2)

file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/npcap
)

find_library(NPCAP_WPCAP_LIBRARY NAMES wpcap PATHS ${CMAKE_SOURCE_DIR}/libs/x64)
find_library(NPCAP_PACKET_LIBRARY NAMES Packet PATHS ${CMAKE_SOURCE_DIR}/libs/x64)

if(NOT NPCAP_WPCAP_LIBRARY)
    message(FATAL_ERROR "缺少 libs/x64/wpcap.lib")
endif()
if(NOT NPCAP_PACKET_LIBRARY)
    message(FATAL_ERROR "缺少 libs/x64/Packet.lib")
endif()

message(STATUS "wpcap: ${NPCAP_WPCAP_LIBRARY}")
message(STATUS "Packet: ${NPCAP_PACKET_LIBRARY}")

set(LIBS
    ws2_32
    wininet
    iphlpapi
    urlmon
    ${NPCAP_PACKET_LIBRARY}
    ${NPCAP_WPCAP_LIBRARY}
)

# ==============================
# 关键：只静态链接运行库，不卡！
# 这是能单文件运行的核心
# ==============================
target_link_options(${PROJECT_NAME} PRIVATE
    -static-libgcc
    -static-libstdc++
    -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})
