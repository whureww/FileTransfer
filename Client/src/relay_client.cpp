// relay_client.cpp - 中继客户端 (发送方/接收方) 实现
// 从原 relay.cpp 拆分而来, 仅包含客户端使用的函数
#include "relay.h"
#include "socket_util.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
using ft::detail::recv_line;
using ft::detail::send_line;
using ft::detail::sanitize_filename;
using ft::detail::unique_filepath;
using ft::write_frame;
using ft::read_frame;

namespace ft {

// ============== 中继客户端: 发送方 ==============

int relay_send_file(const std::string& relay_host, unsigned short relay_port,
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

    // 1. 连接中继服务器
    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        report(cb, 0, 0, "[错误] 创建 socket 失败, errno=" + std::to_string(sock_errno()));
        return ERR_SOCKET;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(relay_port);
    if (::inet_pton(AF_INET, relay_host.c_str(), &addr.sin_addr) <= 0) {
        report(cb, 0, 0, "[错误] 无效的中继服务器地址: " + relay_host);
        close_socket(sock);
        return ERR_CONNECT;
    }
    if (!report(cb, 0, 0, "[信息] 正在连接中继服务器 " + relay_host + ":" + std::to_string(relay_port) + " ...")) {
        close_socket(sock);
        return CANCELED;
    }
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        report(cb, 0, 0, "[错误] 连接中继服务器失败, errno=" + std::to_string(sock_errno()));
        close_socket(sock);
        return ERR_CONNECT;
    }

    // 2. 发送 CREATE
    if (!send_line(sock, "CREATE")) {
        report(cb, 0, 0, "[错误] 发送 CREATE 失败");
        close_socket(sock);
        return ERR_RELAY_LINE;
    }

    // 3. 读取 "CODE <code>"
    std::string line;
    if (!recv_line(sock, line)) {
        report(cb, 0, 0, "[错误] 读取房间码失败");
        close_socket(sock);
        return ERR_RELAY_LINE;
    }
    const std::string code_prefix = "CODE ";
    if (line.compare(0, code_prefix.size(), code_prefix) != 0) {
        report(cb, 0, 0, "[错误] 中继返回异常: " + line);
        close_socket(sock);
        return ERR_RELAY_CODE;
    }
    std::string code = line.substr(code_prefix.size());

    // 通过回调把房间码传回 UI (UI 解析 [房间码] 前缀单独处理)
    if (!report(cb, 0, 0, "[房间码] " + code)) {
        close_socket(sock);
        return CANCELED;
    }
    if (!report(cb, 0, 0, "[信息] 房间已创建, 等待接收方加入... (房间码: " + code + ")")) {
        close_socket(sock);
        return CANCELED;
    }

    // 4. 等待 PEER (中继会在接收方加入后发送 PEER; 也可能发送 TIMEOUT/ERROR)
    // 使用 select 轮询, 以便用户取消时能及时退出 (每 200ms 检查一次 cancel)
    {
        bool got_line = false;
        while (!got_line) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            timeval tv{0, 200 * 1000};
#ifdef _WIN32
            int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
#else
            int sel = ::select(sock + 1, &rfds, nullptr, nullptr, &tv);
#endif
            if (sel == 0) {
                // 超时, 检查用户是否取消
                if (!report(cb, 0, 0, "")) {
                    report(cb, 0, 0, "[信息] 用户已取消发送");
                    close_socket(sock);
                    return CANCELED;
                }
                continue;
            }
            if (sel < 0) {
                break;  // select 出错, 退回 recv_line 尝试
            }
            got_line = recv_line(sock, line);
            if (!got_line) break;
        }
        if (!got_line) {
            report(cb, 0, 0, "[错误] 等待接收方时连接断开");
            close_socket(sock);
            return ERR_RELAY_LINE;
        }
    }
    if (line != "PEER") {
        report(cb, 0, 0, "[错误] 中继返回: " + line + " (期待 PEER)");
        close_socket(sock);
        return ERR_RELAY_PEER;
    }
    if (!report(cb, 0, 0, "[信息] 接收方已连接, 开始发送: " + fname + " (" + std::to_string(file_size) + " bytes)")) {
        close_socket(sock);
        return CANCELED;
    }

    // 5. 在同一 socket 上发送文件 (PacketHeader 协议)
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

    // 6. 流式发送文件内容 (使用二进制帧协议, 支持取消通知)
    std::vector<char> buf(BUFFER_SIZE);
    std::vector<char> frame_data;
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
        // 检查用户取消 (在发送数据帧之前)
        if (!report(cb, sent, file_size, "")) {
            report(cb, 0, 0, "[信息] 用户已取消发送, 通知接收方...");
            write_frame(sock, FRAME_CANCEL, nullptr, 0);
            close_socket(sock);
            return CANCELED;
        }
        // 发送 DATA 帧
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
    // 发送 DONE 帧, 通知接收方数据已全部发送完毕
    if (!write_frame(sock, FRAME_DONE, nullptr, 0)) {
        report(cb, 0, 0, "[错误] 发送完成帧失败");
        close_socket(sock);
        return ERR_SEND_DATA;
    }
    report(cb, file_size, file_size, "[成功] 文件发送完成: " + fname);

    // 主动关闭, 让中继 forward loop 退出
    close_socket(sock);
    return 0;
}

// ============== 中继客户端: 接收方 ==============

int relay_recv_file(const std::string& relay_host, unsigned short relay_port,
                    const std::string& room_code, const std::string& output_dir,
                    ProgressCallback cb) {
    std::string out_dir = normalize_dir(output_dir, cb);

    // 校验房间码格式 (6 位字母数字, 大小写不敏感)
    std::string code = room_code;
    while (!code.empty() && (code.front() == ' ' || code.front() == '\t')) code.erase(code.begin());
    while (!code.empty() && (code.back() == ' ' || code.back() == '\t')) code.pop_back();
    // 转大写
    for (char& c : code) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    if (code.size() != ROOM_CODE_LEN ||
        std::any_of(code.begin(), code.end(), [](char c){
            return !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        })) {
        report(cb, 0, 0, "[错误] 房间码必须是 6 位字母数字");
        return ERR_RELAY_CODE;
    }

    // 1. 连接中继服务器
    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        report(cb, 0, 0, "[错误] 创建 socket 失败, errno=" + std::to_string(sock_errno()));
        return ERR_SOCKET;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(relay_port);
    if (::inet_pton(AF_INET, relay_host.c_str(), &addr.sin_addr) <= 0) {
        report(cb, 0, 0, "[错误] 无效的中继服务器地址: " + relay_host);
        close_socket(sock);
        return ERR_CONNECT;
    }
    if (!report(cb, 0, 0, "[信息] 正在连接中继服务器 " + relay_host + ":" + std::to_string(relay_port) + " ...")) {
        close_socket(sock);
        return CANCELED;
    }
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        report(cb, 0, 0, "[错误] 连接中继服务器失败, errno=" + std::to_string(sock_errno()));
        close_socket(sock);
        return ERR_CONNECT;
    }

    // 2. 发送 JOIN <code>
    if (!send_line(sock, "JOIN " + code)) {
        report(cb, 0, 0, "[错误] 发送 JOIN 失败");
        close_socket(sock);
        return ERR_RELAY_LINE;
    }

    // 3. 读取 OK
    std::string line;
    if (!recv_line(sock, line)) {
        report(cb, 0, 0, "[错误] 读取中继响应失败");
        close_socket(sock);
        return ERR_RELAY_LINE;
    }
    if (line != "OK") {
        report(cb, 0, 0, "[错误] 加入房间失败: " + line);
        close_socket(sock);
        return ERR_RELAY_ROOM;
    }
    if (!report(cb, 0, 0, "[信息] 已加入房间 " + code + ", 等待接收文件...")) {
        close_socket(sock);
        return CANCELED;
    }

    // 4. 在同一 socket 上接收文件 (PacketHeader 协议)
    PacketHeader hdr{};
    if (!recv_all(sock, reinterpret_cast<char*>(&hdr), sizeof(hdr))) {
        report(cb, 0, 0, "[错误] 接收头部失败");
        close_socket(sock);
        return ERR_RECV_HDR;
    }
    if (std::memcmp(hdr.magic, MAGIC, 4) != 0) {
        report(cb, 0, 0, "[错误] 协议魔数不匹配, 数据非法");
        close_socket(sock);
        return ERR_BAD_MAGIC;
    }
    if (hdr.version != PROTOCOL_VERSION) {
        report(cb, 0, 0, "[错误] 协议版本不匹配 (期望 " + std::to_string(PROTOCOL_VERSION)
                          + ", 收到 " + std::to_string(hdr.version) + "), 请升级到相同版本");
        close_socket(sock);
        return ERR_BAD_MAGIC;
    }
    if (hdr.filename_len == 0 || hdr.filename_len > 4096) {
        report(cb, 0, 0, "[错误] 文件名长度异常: " + std::to_string(hdr.filename_len));
        close_socket(sock);
        return ERR_BAD_NAME;
    }

    std::vector<char> name_buf(hdr.filename_len);
    if (!recv_all(sock, name_buf.data(), hdr.filename_len)) {
        report(cb, 0, 0, "[错误] 接收文件名失败");
        close_socket(sock);
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
            close_socket(sock);
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
        close_socket(sock);
        return ERR_CREATE_FILE;
    }

    // 使用二进制帧协议读取数据 (支持取消通知和完成通知)
    uint64_t received = 0;
    uint8_t frame_type = 0;
    std::vector<char> frame_data;
    uint32_t frame_len = 0;
    bool done = false;
    while (!done) {
        if (!read_frame(sock, frame_type, frame_data, frame_len)) {
            report(cb, 0, 0, "[错误] 连接断开 (接收数据失败), errno=" + std::to_string(sock_errno()));
            out.close();
            std::remove(out_path.c_str());
            close_socket(sock);
            return ERR_RECV_DATA;
        }
        if (frame_type == FRAME_CANCEL) {
            // 发送方主动取消
            report(cb, 0, 0, "[信息] 发送方已取消传输");
            out.close();
            std::remove(out_path.c_str());
            close_socket(sock);
            return CANCELED;
        }
        if (frame_type == FRAME_DONE) {
            // 安全校验: 确保接收到的字节数与声明的文件大小一致 (防不完整文件)
            if (received != hdr.file_size) {
                report(cb, 0, 0, "[错误] 文件不完整: 声明 " + std::to_string(hdr.file_size) +
                       " 字节, 实际接收 " + std::to_string(received) + " 字节");
                out.close();
                std::remove(out_path.c_str());
                close_socket(sock);
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
                close_socket(sock);
                return ERR_RECV_DATA;
            }
            out.write(frame_data.data(), frame_len);
            if (!out) {
                report(cb, 0, 0, "[错误] 写入文件失败");
                out.close();
                std::remove(out_path.c_str());
                close_socket(sock);
                return ERR_WRITE_FILE;
            }
            received += frame_len;
            if (!report(cb, received, hdr.file_size, "")) {
                report(cb, 0, 0, "[信息] 用户已取消接收");
                out.close();
                std::remove(out_path.c_str());
                close_socket(sock);
                return CANCELED;
            }
        } else {
            report(cb, 0, 0, "[错误] 未知帧类型: " + std::to_string(frame_type));
            out.close();
            std::remove(out_path.c_str());
            close_socket(sock);
            return ERR_RECV_DATA;
        }
    }
    out.flush();
    out.close();
    report(cb, hdr.file_size, hdr.file_size, "[成功] 文件接收完成: " + out_path);

    close_socket(sock);
    return 0;
}

} // namespace ft
