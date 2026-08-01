cmake_minimum_required(VERSION 3.15)
project(FileTransfer VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS
                    -DUNICODE -D_UNICODE)
    # 动态链接 CRT: exe 运行时依赖 VCRUNTIME140.dll / MSVCP140.dll 等
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

# GUI 客户端 (含局域网直连 + 房间码中继两种模式)
# 优先使用 secret_real.cpp (含真实中继地址, 仅本地), 回退到 secret.cpp (开源占位版)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/secret_real.cpp")
    set(SECRET_SRC src/secret_real.cpp)
    message(STATUS "Using secret_real.cpp (local relay address)")
else()
    set(SECRET_SRC src/secret.cpp)
    message(STATUS "Using secret.cpp (placeholder relay address)")
endif()

set(GUI_SOURCES
    src/socket_util.cpp
    src/file_transfer.cpp
    src/relay_client.cpp
    ${SECRET_SRC}
    src/gui_main.cpp
    app.rc      # Windows 资源(程序图标 + 版本信息)
)

# WIN32 关键字: 生成 GUI 子系统程序 (无控制台黑窗口)
add_executable(FileTransfer WIN32 ${GUI_SOURCES})

if(WIN32)
    target_link_libraries(FileTransfer PRIVATE
        ws2_32 comctl32 comdlg32 shell32 ole32 user32 gdi32 iphlpapi)
else()
    target_link_libraries(FileTransfer PRIVATE pthread)
endif()

# 构建后自动复制必需的 MSVC 运行时 DLL (仅 exe 实际依赖的 3 个)
if(MSVC AND DEFINED ENV{VCToolsRedistDir})
    file(GLOB _crt_dirs "$ENV{VCToolsRedistDir}/x64/Microsoft.VC*.CRT")
    if(_crt_dirs)
        list(GET _crt_dirs 0 _crt_dll_dir)
        add_custom_command(TARGET FileTransfer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${_crt_dll_dir}/VCRUNTIME140.dll"
            "${_crt_dll_dir}/VCRUNTIME140_1.dll"
            "${_crt_dll_dir}/MSVCP140.dll"
            "$<TARGET_FILE_DIR:FileTransfer>"
            COMMENT "Copying MSVC runtime DLLs...")
    endif()
endif()

install(TARGETS FileTransfer RUNTIME DESTINATION bin)

# ===== 构建后用 Inno Setup (ISCC) 编译安装器 =====
# 商业软件标准安装向导: 选择安装路径 / 桌面快捷方式 / 注册表 / 卸载
if(WIN32)
    # 注意: 含括号的环境变量(ProgramFiles(x86))在旧版 CMake 中不能用 $ENV{} 直接读取,
    # 所以这里直接枚举常见安装位置。
    find_program(ISCC_EXECUTABLE ISCC
        PATHS
            "C:/Program Files/Inno Setup 7"
            "C:/Program Files/Inno Setup 6"
            "C:/Program Files (x86)/Inno Setup 7"
            "C:/Program Files (x86)/Inno Setup 6"
            "D:/Inno Setup 7"
            "D:/Inno Setup 6"
        NO_DEFAULT_PATH)

    if(ISCC_EXECUTABLE)
        message(STATUS "Inno Setup ISCC found: ${ISCC_EXECUTABLE}")

        # 占位符: configure_file 时替换 OutputDir / OutputBaseFilename / SetupIconFile
        set(INSTALLER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/installer")
        set(INSTALLER_OUTPUT_BASENAME "FileTransfer_Client_Setup")
        file(TO_NATIVE_PATH "${INSTALLER_OUTPUT_DIR}" INSTALLER_OUTPUT_DIR)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" ISCC_SOURCEDIR)
        # 安装器自身窗口图标 = 客户端图标
        set(INSTALLER_SETUP_ICON "${CMAKE_SOURCE_DIR}/../assets/app_icon.ico")
        file(TO_NATIVE_PATH "${INSTALLER_SETUP_ICON}" INSTALLER_SETUP_ICON)

        configure_file(
            ${CMAKE_SOURCE_DIR}/../installer/Client.iss
            ${CMAKE_BINARY_DIR}/installer/Client.iss
            @ONLY
        )

        add_custom_command(TARGET FileTransfer POST_BUILD
            COMMAND ${ISCC_EXECUTABLE}
                "/DSOURCEDIR=${ISCC_SOURCEDIR}"
                Client.iss
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/installer"
            COMMENT "Building installer (Inno Setup): FileTransfer_Client_Setup.exe"
        )
    else()
        message(STATUS "ISCC.exe (Inno Setup) not found - installer build SKIPPED")
        message(STATUS "  安装 Inno Setup 7 后可手动运行:")
        message(STATUS "    ISCC /DSOURCEDIR=${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/../installer/Client.iss")
    endif()
endif()
