#include "file_transfer.h"
#include "socket_util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// 使用 detail 命名空间下的辅助函数
using ft::detail::socket_t;
using ft::detail::INVALID_SOCK;
using ft::detail::socklen_t;
using ft::detail::close_socket;
using ft::detail::sock_errno;
using ft::detail::get_file_size;
using ft::detail::basename;
using ft::detail::normalize_dir;
using ft::detail::send_all;
using ft::detail::recv_all;
using ft::detail::report;
using ft::detail::sanitize_filename;
using ft::detail::unique_filepath;
using ft::write_frame;
using ft::read_frame;

namespace ft {

// ===== 二进制帧协议实现 =====

// 单帧 payload 最大长度 (与 BUFFER_SIZE 一致, 防止恶意方声明超大帧导致内存耗尽)
constexpr uint32_t MAX_FRAME_PAYLOAD = static_cast<uint32_t>(BUFFER_SIZE);

#pragma pack(push, 1)
struct FrameHeader {
    uint8_t type;       // 帧类型 (FRAME_DATA/CANCEL/DONE)
    uint32_t length;    // payload 长度
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 5, "FrameHeader must be 5 bytes");

bool write_frame(socket_t sock, uint8_t type, const char* data, uint32_t len) {
    FrameHeader hdr{type, len};
    if (!send_all(sock, reinterpret_cast<const char*>(&hdr), sizeof(hdr))) return false;
    if (len > 0 && !send_all(sock, data, len)) return false;
    return true;
}

bool read_frame(socket_t sock, uint8_t& type_out,
                std::vector<char>& data_out, uint32_t& len_out) {
    FrameHeader hdr;
    if (!recv_all(sock, reinterpret_cast<char*>(&hdr), sizeof(hdr))) return false;
    type_out = hdr.type;
    len_out = hdr.length;
    // 安全校验: 拒绝超大帧 (防内存耗尽 DoS)
    if (hdr.length > MAX_FRAME_PAYLOAD) return false;
    data_out.clear();
    if (hdr.length > 0) {
        data_out.resize(hdr.length);
        if (!recv_all(sock, data_out.data(), hdr.length)) return false;
    }
    return true;
}

bool init_network() {
#ifdef _WIN32
    WSADATA wsa;
    int err = ::WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err != 0) return false;
#endif
    return true;
}

void cleanup_network() {
#ifdef _WIN32
    ::WSACleanup();
#endif
}

int send_file(const std::string& ip, unsigned short port,
              const std::string& filepath, ProgressCallback cb) {
#ifdef _WIN32
    std::ifstream in(detail::utf8_to_wpath(filepath), std::ios::binary);
#else
    std::ifstream in(filepath, std::ios::binary);
#endif
    if (!in.is_open()) {
        report(cb, 0, 0, "[错误] 无法打开文件: " + filepath);
        return ERR_OPEN_FILE;
    }

    uint64_t file_size = 0;
    if (!get_file_size(in, file_size)) {
        report(cb, 0, 0, "[错误] 无法获取文件大小: " + filepath);
        return ERR_FILE_SIZE;
    }

    std::string fname = basename(filepath);

    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        report(cb, 0, 0, "[错误] 创建 socket 失败, errno=" + std::to_string(sock_errno()));
        return ERR_SOCKET;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        report(cb, 0, 0, "[错误] 无效的 IP 地址: " + ip);
        close_socket(sock);
        return ERR_CONNECT;
    }

    if (!report(cb, 0, 0, "[信息] 正在连接 " + ip + ":" + std::to_string(port) + " ...")) {
        close_socket(sock);
        return CANCELED;
    }
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        report(cb, 0, 0, "[错误] 连接失败, errno=" + std::to_string(sock_errno()));
        close_socket(sock);
        return ERR_CONNECT;
    }
    if (!report(cb, 0, 0, "[信息] 连接成功, 开始发送: " + fname + " (" + std::to_string(file_size) + " bytes)")) {
        close_socket(sock);
        return CANCELED;
    }

    PacketHeader hdr{};
    std::memcpy(hdr.magic, MAGIC, 4);
    hdr.version = PROTOCOL_VERSION;
    hdr.flags = 0;
    hdr.filename_len = static_cast<uint16_t>(fname.size());
    hdr.file_size = file_size;

    if (!send_all(sock, reinterpret_cast<const char*>(&hdr), sizeof(hdr))) {
        report(cb, 0, 0, "[错误] 发送头部失败, errno=" + std::to_string(sock_errno()));
        close_socket(sock);
        return ERR_SEND_HDR;
    }
    if (!send_all(sock, fname.data(), fname.size())) {
        report(cb, 0, 0, "[错误] 发送文件名失败, errno=" + std::to_string(sock_errno()));
        close_socket(sock);
        return ERR_SEND_HDR;
    }

    std::vector<char> buf(BUFFER_SIZE);
    uint64_t sent = 0;
    while (sent < file_size) {
        std::streamsize want = static_cast<std::streamsize>(
            std::min<uint64_t>(BUFFER_SIZE, file_size - sent));
        in.read(buf.data(), want);
        std::streamsize got = in.gcount();
        if (got <= 0) {
            report(cb, 0, 0, "[错误] 读取文件失败");
            close_socket(sock);
            return ERR_READ_FILE;
        }
        if (!report(cb, sent, file_size, "")) {
            report(cb, 0, 0, "[信息] 用户已取消发送, 通知接收方...");
            write_frame(sock, FRAME_CANCEL, nullptr, 0);
            close_socket(sock);
            return CANCELED;
        }
        if (!write_frame(sock, FRAME_DATA, buf.data(), static_cast<uint32_t>(got))) {
            report(cb, 0, 0, "[错误] 发送数据失败 (接收方可能已取消), errno=" + std::to_string(sock_errno()));
            close_socket(sock);
            return ERR_SEND_DATA;
        }
        sent += static_cast<uint64_t>(got);
        if (!report(cb, sent, file_size, "")) {
            report(cb, 0, 0, "[信息] 用户已取消发送, 通知接收方...");
            write_frame(sock, FRAME_CANCEL, nullptr, 0);
            close_socket(sock);
            return CANCELED;
        }
    }
    write_frame(sock, FRAME_DONE, nullptr, 0);
    report(cb, file_size, file_size, "[成功] 文件发送完成: " + fname);

    close_socket(sock);
    return 0;
}

int recv_file(unsigned short port, const std::string& output_dir,
              ProgressCallback cb) {
    std::string out_dir = normalize_dir(output_dir, cb);

    socket_t listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCK) {
        report(cb, 0, 0, "[错误] 创建 socket 失败, errno=" + std::to_string(sock_errno()));
        return ERR_SOCKET;
    }

    int yes = 1;
    ::setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    addr.sin_addr.s_addr = ::htonl(INADDR_ANY);

    if (::bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        report(cb, 0, 0, "[错误] bind 失败, errno=" + std::to_string(sock_errno()));
        close_socket(listen_sock);
        return ERR_BIND;
    }
    if (::listen(listen_sock, 1) == -1) {
        report(cb, 0, 0, "[错误] listen 失败, errno=" + std::to_string(sock_errno()));
        close_socket(listen_sock);
        return ERR_LISTEN;
    }

    if (!report(cb, 0, 0, "[信息] 等待接收, 监听端口 " + std::to_string(port) + ", 保存到: " + out_dir)) {
        close_socket(listen_sock);
        return CANCELED;
    }
    if (!report(cb, 0, 0, "[信息] 等待发送方连接...")) {
        close_socket(listen_sock);
        return CANCELED;
    }

    // 使用 select 轮询 accept, 以便用户取消时能及时退出 (每 200ms 检查一次 cancel)
    sockaddr_in client_addr{};
    int client_len = sizeof(client_addr);
    socket_t conn = INVALID_SOCK;
    while (conn == INVALID_SOCK) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_sock, &rfds);
        timeval tv{0, 200 * 1000};  // 200ms 超时
#ifdef _WIN32
        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
#else
        int sel = ::select(listen_sock + 1, &rfds, nullptr, nullptr, &tv);
#endif
        if (sel == 0) {
            // 超时, 检查用户是否取消
            if (!report(cb, 0, 0, "")) {
                close_socket(listen_sock);
                return CANCELED;
            }
            continue;
        }
        if (sel < 0) {
            report(cb, 0, 0, "[错误] select 失败, errno=" + std::to_string(sock_errno()));
            close_socket(listen_sock);
            return ERR_SOCKET;
        }
        conn = ::accept(listen_sock,
                        reinterpret_cast<sockaddr*>(&client_addr),
                        &client_len);
        if (conn == INVALID_SOCK) {
            report(cb, 0, 0, "[错误] accept 失败, errno=" + std::to_string(sock_errno()));
            close_socket(listen_sock);
            return ERR_SOCKET;
        }
    }

    char ip_buf[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
    report(cb, 0, 0, std::string("[信息] 发送方已连接: ") + ip_buf + ":"
             + std::to_string(::ntohs(client_addr.sin_port)));

    PacketHeader hdr{};
    if (!recv_all(conn, reinterpret_cast<char*>(&hdr), sizeof(hdr))) {
        report(cb, 0, 0, "[错误] 接收头部失败");
        close_socket(conn);
        close_socket(listen_sock);
        return ERR_RECV_HDR;
    }
    if (std::memcmp(hdr.magic, MAGIC, 4) != 0) {
        report(cb, 0, 0, "[错误] 协议魔数不匹配, 数据非法");
        close_socket(conn);
        close_socket(listen_sock);
        return ERR_BAD_MAGIC;
    }
    if (hdr.version != PROTOCOL_VERSION) {
        report(cb, 0, 0, "[错误] 协议版本不匹配 (期望 " + std::to_string(PROTOCOL_VERSION)
                          + ", 收到 " + std::to_string(hdr.version) + "), 请升级到相同版本");
        close_socket(conn);
        close_socket(listen_sock);
        return ERR_BAD_MAGIC;
    }
    if (hdr.filename_len == 0 || hdr.filename_len > 4096) {
        report(cb, 0, 0, "[错误] 文件名长度异常: " + std::to_string(hdr.filename_len));
        close_socket(conn);
        close_socket(listen_sock);
        return ERR_BAD_NAME;
    }

    std::vector<char> name_buf(hdr.filename_len);
    if (!recv_all(conn, name_buf.data(), hdr.filename_len)) {
        report(cb, 0, 0, "[错误] 接收文件名失败");
        close_socket(conn);
        close_socket(listen_sock);
        return ERR_RECV_NAME;
    }
    std::string fname(name_buf.begin(), name_buf.end());

    // 安全过滤文件名 (防目录穿越、保留设备名等)
    std::string safe_name = sanitize_filename(fname);

    // 生成不冲突的文件路径 (若文件已存在则追加 (1), (2) 等)
    std::string out_path = unique_filepath(out_dir, safe_name);
    report(cb, 0, 0, "[信息] 准备接收: " + safe_name + " ("
             + std::to_string(hdr.file_size) + " bytes) -> " + out_path);

    // 磁盘空间预检
    {
        namespace fs = std::filesystem;
        std::error_code ec;
#ifdef _WIN32
        fs::space_info si = fs::space(fs::path(detail::utf8_to_wpath(out_dir)), ec);
#else
        fs::space_info si = fs::space(out_dir, ec);
#endif
        if (!ec && si.available < hdr.file_size) {
            report(cb, 0, 0, "[错误] 磁盘空间不足: 需要 " + std::to_string(hdr.file_size) +
                           " 字节, 可用 " + std::to_string(si.available) + " 字节");
            close_socket(conn);
            close_socket(listen_sock);
            return ERR_CREATE_FILE;
        }
    }

#ifdef _WIN32
    std::ofstream out(detail::utf8_to_wpath(out_path), std::ios::binary | std::ios::trunc);
#else
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
#endif
    if (!out.is_open()) {
        report(cb, 0, 0, "[错误] 无法创建输出文件: " + out_path);
        close_socket(conn);
        close_socket(listen_sock);
        return ERR_CREATE_FILE;
    }

    // 使用二进制帧协议读取数据 (支持取消通知)
    uint64_t received = 0;
    uint8_t frame_type = 0;
    std::vector<char> frame_data;
    uint32_t frame_len = 0;
    bool done = false;
    while (!done) {
        if (!read_frame(conn, frame_type, frame_data, frame_len)) {
            report(cb, 0, 0, "[错误] 连接断开 (接收数据失败), errno=" + std::to_string(sock_errno()));
            out.close();
            std::remove(out_path.c_str());
            close_socket(conn);
            close_socket(listen_sock);
            return ERR_RECV_DATA;
        }
        if (frame_type == FRAME_CANCEL) {
            report(cb, 0, 0, "[信息] 发送方已取消传输");
            out.close();
            std::remove(out_path.c_str());
            close_socket(conn);
            close_socket(listen_sock);
            return CANCELED;
        }
        if (frame_type == FRAME_DONE) {
            // 安全校验: 确保接收到的字节数与声明的文件大小一致 (防不完整文件)
            if (received != hdr.file_size) {
                report(cb, 0, 0, "[错误] 文件不完整: 声明 " + std::to_string(hdr.file_size) +
                       " 字节, 实际接收 " + std::to_string(received) + " 字节");
                out.close();
                std::remove(out_path.c_str());
                close_socket(conn);
                close_socket(listen_sock);
                return ERR_RECV_DATA;
            }
            done = true;
            break;
        }
        if (frame_type == FRAME_DATA) {
            // 安全校验: 防止发送方发送超出声明大小的数据 (磁盘填满攻击)
            if (received + frame_len > hdr.file_size) {
                report(cb, 0, 0, "[错误] 接收数据超出声明的文件大小, 可能存在攻击");
                out.close();
                std::remove(out_path.c_str());
                close_socket(conn);
                close_socket(listen_sock);
                return ERR_RECV_DATA;
            }
            out.write(frame_data.data(), frame_len);
            if (!out) {
                report(cb, 0, 0, "[错误] 写入文件失败");
                out.close();
                std::remove(out_path.c_str());
                close_socket(conn);
                close_socket(listen_sock);
                return ERR_WRITE_FILE;
            }
            received += frame_len;
            if (!report(cb, received, hdr.file_size, "")) {
                report(cb, 0, 0, "[信息] 用户已取消接收");
                out.close();
                std::remove(out_path.c_str());
                close_socket(conn);
                close_socket(listen_sock);
                return CANCELED;
            }
        } else {
            report(cb, 0, 0, "[错误] 未知帧类型: " + std::to_string(frame_type));
            out.close();
            std::remove(out_path.c_str());
            close_socket(conn);
            close_socket(listen_sock);
            return ERR_RECV_DATA;
        }
    }
    out.flush();
    out.close();
    report(cb, hdr.file_size, hdr.file_size, "[成功] 文件接收完成: " + out_path);

    close_socket(conn);
    close_socket(listen_sock);
    return 0;
}

// ===== 局域网自动发现 (UDP 广播) =====

std::vector<std::pair<std::string, unsigned short>>
discover_peers(unsigned short tcp_port, int timeout_ms) {
    std::vector<std::pair<std::string, unsigned short>> result;

    socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK) return result;

    // 允许广播
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    // 设置接收超时 (200ms 轮询, 用于周期性检查超时)
#ifdef _WIN32
    DWORD rcvtimeo = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&rcvtimeo), sizeof(rcvtimeo));
#else
    timeval tv; tv.tv_sec = 0; tv.tv_usec = 200 * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // 发送广播发现包
    sockaddr_in bcast_addr{};
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    bcast_addr.sin_port = htons(DISCOVERY_PORT);
    const char* msg = "FT_DISCOVER\n";
    ::sendto(sock, msg, static_cast<int>(std::strlen(msg)), 0,
             reinterpret_cast<sockaddr*>(&bcast_addr), sizeof(bcast_addr));

    // 收集响应
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) break;

        char buf[256] = {0};
        sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        int n = ::recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           reinterpret_cast<sockaddr*>(&from), &fromlen);
        if (n <= 0) continue;  // 超时或错误, 继续轮询

        buf[n] = 0;
        // 解析 "FT_HERE <port>"
        if (std::strncmp(buf, "FT_HERE", 7) != 0) continue;
        unsigned short port = 0;
        try {
            long p = std::stol(buf + 8);
            if (p < 1 || p > 65535) continue;
            port = static_cast<unsigned short>(p);
        } catch (...) {
            continue;
        }
        if (port != tcp_port) continue;

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        std::string ipstr(ip);
        // 去重
        bool dup = false;
        for (auto& p : result) {
            if (p.first == ipstr) { dup = true; break; }
        }
        if (!dup) result.push_back({ipstr, port});
    }

    close_socket(sock);
    return result;
}

std::thread start_discovery_responder(unsigned short tcp_port,
                                      std::atomic<bool>& running) {
    return std::thread([tcp_port, &running]() {
        socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCK) return;

        // 允许地址重用 (防止上次未释放导致绑定失败)
        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        // 绑定到发现端口
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(DISCOVERY_PORT);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            close_socket(sock);
            return;
        }

        // 设置接收超时 (200ms, 用于周期性检查 running 标志)
#ifdef _WIN32
        DWORD rcvtimeo = 200;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&rcvtimeo), sizeof(rcvtimeo));
#else
        timeval tv; tv.tv_sec = 0; tv.tv_usec = 200 * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        char buf[256] = {0};
        while (running.load()) {
            sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            int n = ::recvfrom(sock, buf, sizeof(buf) - 1, 0,
                               reinterpret_cast<sockaddr*>(&from), &fromlen);
            if (n <= 0) continue;  // 超时或错误
            buf[n] = 0;
            if (std::strcmp(buf, "FT_DISCOVER\n") == 0) {
                char resp[64] = {0};
                int len = std::snprintf(resp, sizeof(resp),
                                        "FT_HERE %u\n", static_cast<unsigned>(tcp_port));
                ::sendto(sock, resp, len, 0,
                         reinterpret_cast<sockaddr*>(&from), fromlen);
            }
        }
        close_socket(sock);
    });
}

} // namespace ft
