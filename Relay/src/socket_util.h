// socket_util.h - 跨平台 socket/IO 辅助函数 (file_transfer 和 relay 共用)
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

#include "file_transfer.h"  // ProgressCallback

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace ft {
namespace detail {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
using socklen_t = int;
inline int close_socket(socket_t s) { return ::closesocket(s); }
inline int sock_errno() { return ::WSAGetLastError(); }
#else
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
inline int close_socket(socket_t s) { return ::close(s); }
inline int sock_errno() { return errno; }
#endif

// 跨平台获取文件大小
bool get_file_size(std::ifstream& in, uint64_t& size);

// 从路径中提取文件名 (跨平台支持 / 和 \)
std::string basename(const std::string& path);

// 规范化输出目录路径 (使用平台原生分隔符, 末尾带分隔符)
std::string normalize_dir(const std::string& dir, const ProgressCallback& cb);

// 安全地发送全部数据, 处理部分写入
bool send_all(socket_t sock, const char* buf, std::size_t len);

// 安全地接收全部数据, 处理部分读取
bool recv_all(socket_t sock, char* buf, std::size_t len);

// 通过回调报告状态/进度; 无回调时输出到控制台. 返回 false 表示用户取消
bool report(const ProgressCallback& cb, uint64_t done, uint64_t total,
            const std::string& msg);

// 逐字节读取一行 (直到 \n), 不超过 maxLen. 返回 false 表示连接断开
// 注意: 调用前应设置 SO_RCVTIMEO 防止 slowloris 攻击
bool recv_line(socket_t sock, std::string& line, std::size_t maxLen = 1024);

// 发送一行文本 (追加 \n)
bool send_line(socket_t sock, const std::string& s);

// 安全过滤文件名: 去掉路径分隔符、控制字符、Windows 非法字符
// 防止目录穿越攻击和保留设备名问题
std::string sanitize_filename(const std::string& name);

// 生成不冲突的文件路径: 若文件已存在则追加 (1), (2) 等
std::string unique_filepath(const std::string& dir, const std::string& filename);

} // namespace detail
} // namespace ft
