cmake_minimum_required(VERSION 3.18)
project(filetransfer_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# C++ 核心源码 (从桌面端 Client/src 抽取)
set(FT_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(filetransfer_native SHARED
    ${FT_CORE_DIR}/file_transfer.cpp
    ${FT_CORE_DIR}/relay_client.cpp
    ${FT_CORE_DIR}/socket_util.cpp
    ${FT_CORE_DIR}/secret.cpp
    ${FT_CORE_DIR}/jni_bridge.cpp
)

target_include_directories(filetransfer_native PUBLIC
    ${FT_CORE_DIR}
)

# Android NDK
if(ANDROID)
    target_link_libraries(filetransfer_native log)
    find_library(android-lib android)
    target_link_libraries(filetransfer_native ${android-lib})
    # Android NDK < r23 需要显式链接 stdc++fs (filesystem)
    if(CMAKE_ANDROID_NDK_MAJOR AND CMAKE_ANDROID_NDK_MAJOR LESS 23)
        target_link_libraries(filetransfer_native stdc++fs)
    endif()
endif()

# Windows (桌面调试用)
if(WIN32)
    target_link_libraries(filetransfer_native ws2_32)
endif()

# iOS / macOS
if(APPLE)
    find_library(FOUNDATION Foundation)
    target_link_libraries(filetransfer_native ${FOUNDATION})
    # macOS < 10.15 需要显式链接 stdc++fs
    if(CMAKE_SYSTEM_VERSION VERSION_LESS 10.15)
        target_link_libraries(filetransfer_native stdc++fs)
    endif()
endif()
