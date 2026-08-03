// SilexRelay 中继服务器控制台入口
// 用法: SilexRelay.exe [port]    默认端口 9091
// 部署到公网 VPS 上, 两端通过房间码进行跨局域网文件传输
#include "relay.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
// 源码以 UTF-8 编译 (/utf-8), 需把控制台代码页切到 UTF-8, 否则中文乱码
static void enable_utf8_console() {
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);
}
#else
static void enable_utf8_console() {}
#endif

// 中继服务器运行标志 (在 relay_server.cpp 中定义)
namespace ft {
extern std::atomic<bool> g_relay_running;
}

// 信号处理: Ctrl+C / Ctrl+Break 时优雅退出
static void signal_handler(int sig) {
    (void)sig;
    ft::g_relay_running.store(false);
}

int main(int argc, char** argv) {
    enable_utf8_console();

    // 单实例检测: 防止同一台电脑运行多个中继服务器
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Silex_Relay_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "[错误] 中继服务器已在运行, 请勿重复启动\n";
        CloseHandle(hMutex);
        return 1;
    }

    // 注册信号处理 (Ctrl+C 优雅退出)
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef _WIN32
    std::signal(SIGBREAK, signal_handler);
#endif

    unsigned short port = ft::DEFAULT_RELAY_PORT;
    if (argc >= 2) {
        try {
            long v = std::stol(argv[1]);
            if (v < 1 || v > 65535) {
                std::cerr << "[错误] 端口号必须在 1-65535 范围内: " << argv[1] << "\n";
                return 1;
            }
            port = static_cast<unsigned short>(v);
        } catch (...) {
            std::cerr << "[错误] 无效的端口号: " << argv[1] << "\n";
            return 1;
        }
    }

    if (!ft::init_network()) {
        std::cerr << "[错误] 网络初始化失败\n";
        return 2;
    }

    std::cout << "SilexRelay v0.0.9\n";
    std::cout << "用法: 两端均连接本服务, 发送方创建房间得到 6 位房间码,\n";
    std::cout << "      将房间码告知接收方即可进行文件传输 (跨局域网)\n";
    std::cout << "默认端口: " << ft::DEFAULT_RELAY_PORT
              << "  当前端口: " << port << "\n\n";

    int ret = ft::run_relay_server(port);

    ft::cleanup_network();
    return ret;
}
