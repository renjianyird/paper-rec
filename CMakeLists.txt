cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(paper CXX)

# 基础编译标准（保留原配置，强化可读性）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) 

# 建议：替换GLOB为显式列文件（避免新增文件需重新cmake）
# 若src下文件固定，推荐写死（示例），否则保留GLOB也可
# set(SOURCES
#     src/main.cpp
#     src/capture.cpp
#     src/xxx.cpp
# )
file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

# ========== Windows Npcap 依赖配置 ==========
# 1. 查找Npcap库（保留原路径逻辑）
find_library(NPCAP_WPCAP_LIBRARY NAMES wpcap PATHS ${CMAKE_SOURCE_DIR}/libs/x64)
find_library(NPCAP_PACKET_LIBRARY NAMES Packet PATHS ${CMAKE_SOURCE_DIR}/libs/x64)

# 2. 强制检查库是否找到（关键：编译阶段报错，而非运行时崩溃）
if(NOT NPCAP_WPCAP_LIBRARY)
    message(FATAL_ERROR "❌ 未找到wpcap.lib！请检查 ${CMAKE_SOURCE_DIR}/libs/x64 目录是否存在该文件")
endif()
if(NOT NPCAP_PACKET_LIBRARY)
    message(FATAL_ERROR "❌ 未找到Packet.lib！请检查 ${CMAKE_SOURCE_DIR}/libs/x64 目录是否存在该文件")
endif()

# 3. 打印找到的库路径（保留原日志，便于排查）
message(STATUS "✅ Found Npcap wpcap lib: ${NPCAP_WPCAP_LIBRARY}")
message(STATUS "✅ Found Npcap Packet lib: ${NPCAP_PACKET_LIBRARY}")

# ========== 头文件目录 ==========
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/npcap
)

# ========== 链接库（简化逻辑，统一管理） ==========
# 提取公共库列表，避免Release/Debug重复写
set(WIN_COMMON_LIBS
    ws2_32      # Windows网络套接字
    wininet     # Windows网络操作
    iphlpapi    # IP帮助函数（如获取网卡信息）
    urlmon      # URL相关操作
    ${NPCAP_PACKET_LIBRARY}
    ${NPCAP_WPCAP_LIBRARY}
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Release模式：静态编译（保留原逻辑）
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${WIN_COMMON_LIBS}
        -static-libgcc
        -static-libstdc++
    )
else()
    # Debug模式：仅链接基础库
    target_link_libraries(${PROJECT_NAME} PRIVATE ${WIN_COMMON_LIBS})
endif()

# 可选：强制设置Windows编译为x64（避免32/64位不匹配）
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(WARNING "⚠️ 当前编译为32位！Npcap libs/x64是64位库，建议切换到x64编译")
endif()
