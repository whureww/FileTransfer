// relay_server.cpp - 中继服务器实现
// 从原 relay.cpp 拆分而来, 仅包含服务端逻辑
#include "relay.h"
#include "socket_util.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// 使用 detail 命名空间下的辅助函数
using ft::detail::socket_t;
using ft::detail::INVALID_SOCK;
using ft::detail::socklen_t;
using ft::detail::close_socket;
using ft::detail::sock_errno;
using ft::detail::send_all;
using ft::detail::recv_all;
using ft::detail::report;
using ft::detail::recv_line;
using ft::detail::send_line;

namespace ft {

// 全局运行标志 (用于优雅退出)
std::atomic<bool> g_relay_running{true};

namespace {

// ============== 日志 ==============
// 输出到 stdout, 带时间戳. 线程安全 (使用 localtime_s/localtime_r).
std::mutex g_log_mtx;
void relay_log(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    char timebuf[32];
#ifdef _WIN32
    struct tm tm_buf;
    localtime_s(&tm_buf, &t);
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);
#else
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);
#endif
    std::lock_guard<std::mutex> lk(g_log_mtx);
    std::cout << "[" << timebuf << "." << std::setfill('0') << std::setw(3) << ms.count()
              << "] " << msg << std::endl;
}

// ============== 中继服务器: 房间管理 ==============

struct Room {
    std::string code;
    socket_t sender = INVALID_SOCK;
    socket_t receiver = INVALID_SOCK;
    std::atomic<bool> paired{false};
    std::atomic<bool> dead{false};
};

struct RelayState {
    std::mutex mtx;
    std::map<std::string, Room*> rooms;
    std::mt19937 rng{std::random_device{}()};
    std::atomic<int> active_connections{0};
};

// 设置 socket 接收超时 (毫秒)
void set_recv_timeout(socket_t sock, int timeout_ms) {
#ifdef _WIN32
    DWORD ms = static_cast<DWORD>(timeout_ms);
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// 生成 6 位字母数字房间码 (A-Z, 0-9, 排除易混淆字符 0/O/I/1)
// 有效字符集 32 个, 32^6 ≈ 10 亿种组合, 抗暴力枚举
std::string gen_room_code(RelayState& st) {
    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static const int charset_size = static_cast<int>(sizeof(charset) - 1);
    std::lock_guard<std::mutex> lk(st.mtx);
    while (true) {
        char buf[ROOM_CODE_LEN + 1] = {0};
        for (int i = 0; i < ROOM_CODE_LEN; ++i) {
            buf[i] = charset[st.rng() % charset_size];
        }
        std::string code(buf);
        if (st.rooms.find(code) == st.rooms.end()) return code;
    }
}

// 中继转发循环: 把 sender 的字节流原样转发给 receiver, 直到 sender 关闭连接
// 限制: 最大传输 RELAY_MAX_DATA 字节, 超时 RELAY_TIMEOUT_MS 毫秒
void relay_forward_loop(socket_t sender, socket_t receiver) {
    // 设置接收超时, 防止连接永久挂起
    set_recv_timeout(sender, 30000);  // 30 秒无数据则断开
    set_recv_timeout(receiver, 30000);

    std::vector<char> buf(BUFFER_SIZE);
    uint64_t total_forwarded = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        // 检查传输总量限制
        if (total_forwarded >= RELAY_MAX_DATA) {
            relay_log("传输数据量超过上限 " + std::to_string(RELAY_MAX_DATA) + " 字节, 强制断开");
            break;
        }
        // 检查传输时间限制
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        if (elapsed >= RELAY_TIMEOUT_MS) {
            relay_log("传输超时 " + std::to_string(RELAY_TIMEOUT_MS / 1000) + " 秒, 强制断开");
            break;
        }

        int n = ::recv(sender, buf.data(), static_cast<int>(buf.size()), 0);
        if (n <= 0) break;  // 发送方关闭或超时
        if (!send_all(receiver, buf.data(), static_cast<std::size_t>(n))) break;
        total_forwarded += static_cast<uint64_t>(n);
    }
    // 关闭双方 socket, 触发任何阻塞中的 recv 返回
    close_socket(sender);
    close_socket(receiver);
}

// 处理 Sender 连接 (首行 CREATE 已读取)
void handle_sender(RelayState& st, socket_t sock) {
    // 0. 检查房间数上限
    {
        std::lock_guard<std::mutex> lk(st.mtx);
        if (static_cast<int>(st.rooms.size()) >= MAX_ROOMS) {
            relay_log("房间数已达上限 " + std::to_string(MAX_ROOMS) + ", 拒绝创建新房间");
            send_line(sock, "ERROR server busy");
            close_socket(sock);
            return;
        }
    }

    // 1. 分配房间码并注册
    std::string code = gen_room_code(st);
    Room* room = new Room();
    room->code = code;
    room->sender = sock;
    {
        std::lock_guard<std::mutex> lk(st.mtx);
        st.rooms[code] = room;
    }
    size_t room_count;
    {
        std::lock_guard<std::mutex> lk(st.mtx);
        room_count = st.rooms.size();
    }
    relay_log("房间创建 code=" + code + " (在线房间数: " + std::to_string(room_count) + ")");

    // 2. 返回 CODE 给发送方
    if (!send_line(sock, "CODE " + code)) {
        relay_log("房间 " + code + ": 发送 CODE 失败, sender 断开");
        std::lock_guard<std::mutex> lk(st.mtx);
        st.rooms.erase(code);
        delete room;
        close_socket(sock);
        return;
    }

    // 3. 等待接收方配对 (select 轮询 200ms, 总共最多 1 小时)
    const int kMaxWaitMs = 60 * 60 * 1000;
    int waited = 0;
    while (!room->paired.load() && !room->dead.load() && waited < kMaxWaitMs) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        timeval tv{0, 200 * 1000};  // 200ms
#ifdef _WIN32
        int ret = ::select(0, &rfds, nullptr, nullptr, &tv);
#else
        int ret = ::select(sock + 1, &rfds, nullptr, nullptr, &tv);
#endif
        if (ret < 0) break;  // select 出错
        if (ret > 0) {
            // 发送方在等待期间主动断开 (例如取消)? 用 MSG_PEEK 探测
            char probe = 0;
            int n = ::recv(sock, &probe, 1, MSG_PEEK);
            if (n == 0) break;  // 对端关闭
            // n<0 且 EWOULDBLOCK 视为只是 select 误报, 继续等待
#ifdef _WIN32
            if (n < 0 && sock_errno() != WSAEWOULDBLOCK) break;
#else
            if (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN) break;
#endif
        }
        waited += 200;
    }

    if (!room->paired.load()) {
        // 超时或发送方断开
        relay_log("房间 " + code + ": 等待超时或 sender 断开, 清理房间");
        send_line(sock, "TIMEOUT");
        std::lock_guard<std::mutex> lk(st.mtx);
        st.rooms.erase(code);
        room->dead = true;
        if (room->receiver != INVALID_SOCK) {
            send_line(room->receiver, "ERROR peer gone");
            close_socket(room->receiver);
        }
        close_socket(sock);
        delete room;
        return;
    }

    // 4. 通知发送方: PEER 已就绪
    if (!send_line(sock, "PEER")) {
        relay_log("房间 " + code + ": 发送 PEER 失败, sender 断开");
        std::lock_guard<std::mutex> lk(st.mtx);
        st.rooms.erase(code);
        room->dead = true;
        if (room->receiver != INVALID_SOCK) close_socket(room->receiver);
        close_socket(sock);
        delete room;
        return;
    }
    relay_log("房间 " + code + ": 配对成功, 开始转发数据");

    // 5. 转发字节流 sender -> receiver (阻塞, 直到传输完成)
    socket_t receiver = room->receiver;
    relay_forward_loop(sock, receiver);
    relay_log("房间 " + code + ": 传输完成, 清理房间");

    // 6. 清理房间
    {
        std::lock_guard<std::mutex> lk(st.mtx);
        st.rooms.erase(code);
    }
    room->dead = true;
    delete room;
}

// 处理 Receiver 连接 (首行 "JOIN <code>" 已读取)
// 注意: room 的生命周期由 handle_sender 管理 (它创建并删除 room).
//       本函数只设置 room->receiver 并发送 OK, 不删除 room.
//       若失败, 仅设置 room->dead = true 通知 sender 清理.
void handle_receiver(RelayState& st, socket_t sock, const std::string& first_line) {
    const std::string prefix = "JOIN ";
    if (first_line.size() <= prefix.size() ||
        first_line.compare(0, prefix.size(), prefix) != 0) {
        send_line(sock, "ERROR expected JOIN <code>");
        close_socket(sock);
        return;
    }
    std::string code = first_line.substr(prefix.size());
    while (!code.empty() && (code.back() == ' ' || code.back() == '\t')) code.pop_back();
    while (!code.empty() && (code.front() == ' ' || code.front() == '\t')) code.erase(code.begin());

    // 校验房间码格式 (长度 + 字符集: 与 gen_room_code 使用的字符集一致)
    if (code.size() != ROOM_CODE_LEN) {
        send_line(sock, "ERROR invalid room code");
        close_socket(sock);
        return;
    }
    // 字符集校验: A-Z (排除 I/O) + 2-9 (排除 0/1)
    for (char c : code) {
        if (!((c >= 'A' && c <= 'Z' && c != 'I' && c != 'O') ||
              (c >= '0' && c <= '9' && c != '0' && c != '1'))) {
            send_line(sock, "ERROR invalid room code");
            close_socket(sock);
            return;
        }
    }

    Room* room = nullptr;
    {
        std::lock_guard<std::mutex> lk(st.mtx);
        auto it = st.rooms.find(code);
        if (it == st.rooms.end()) {
            relay_log("JOIN " + code + ": 房间不存在");
            send_line(sock, "ERROR no such room");
            close_socket(sock);
            return;
        }
        room = it->second;
        if (room->paired.load() || room->receiver != INVALID_SOCK) {
            relay_log("JOIN " + code + ": 房间已被占用");
            send_line(sock, "ERROR room busy");
            close_socket(sock);
            return;
        }
        room->receiver = sock;  // 先占位, 但还未 paired
    }

    // 通知接收方: 已配对成功
    if (!send_line(sock, "OK")) {
        relay_log("房间 " + code + ": 发送 OK 失败, receiver 断开");
        // 失败: 回滚 receiver, 通知 sender 继续 (或清理)
        // 注意: sender 线程可能已超时清理并 delete 了 room,
        //       必须在锁内通过 map 查找确认 room 仍存活, 避免UAF
        std::lock_guard<std::mutex> lk(st.mtx);
        auto it = st.rooms.find(code);
        if (it != st.rooms.end() && it->second == room) {
            room->receiver = INVALID_SOCK;
            room->dead = true;  // 让 sender 退出等待并清理
        }
        // 若 room 已从 map 移除, 说明 sender 已 delete 它, 不再碰 room
        close_socket(sock);
        return;
    }

    // OK 已发送, 正式触发配对 (sender 线程在 select 循环里检测到 paired 后会发送 PEER)
    // 同样需要确认 room 仍存活: sender 可能在等待期间已超时清理
    {
        std::lock_guard<std::mutex> lk(st.mtx);
        auto it = st.rooms.find(code);
        if (it == st.rooms.end() || it->second != room) {
            // sender 已清理 room, receiver socket 已被 sender 关闭
            close_socket(sock);
            return;
        }
    }
    room->paired = true;
    // 接收方线程到此结束; socket 由 sender 线程的 relay_forward_loop 继续使用
    // (不关闭 sock, 所有权移交给 sender 线程)
}

// 接受连接的工作线程: 读首行, 分发
void conn_thread(RelayState* st, socket_t sock) {
    // 设置 recv_line 超时, 防止 slowloris 攻击
    set_recv_timeout(sock, RELAY_RECV_TIMEOUT_MS);

    std::string line;
    if (!recv_line(sock, line)) {
        close_socket(sock);
        st->active_connections.fetch_sub(1);
        return;
    }
    if (line == "CREATE") {
        handle_sender(*st, sock);
    } else if (line.compare(0, 5, "JOIN ") == 0) {
        handle_receiver(*st, sock, line);
    } else {
        send_line(sock, "ERROR unknown command");
        close_socket(sock);
    }
    st->active_connections.fetch_sub(1);
}

} // namespace

// ============== 中继服务器主循环 ==============

int run_relay_server(unsigned short port) {
    socket_t listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCK) {
        std::cerr << "[relay] 创建 socket 失败, errno=" << sock_errno() << "\n";
        return 1;
    }

    int yes = 1;
    ::setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    addr.sin_addr.s_addr = ::htonl(INADDR_ANY);

    if (::bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::cerr << "[relay] bind 失败, port=" << port << ", errno=" << sock_errno() << "\n";
        close_socket(listen_sock);
        return 2;
    }
    if (::listen(listen_sock, 16) == -1) {
        std::cerr << "[relay] listen 失败, errno=" << sock_errno() << "\n";
        close_socket(listen_sock);
        return 3;
    }

    relay_log("中继服务器已启动, 监听端口 " + std::to_string(port) + " (Ctrl+C 退出)");
    relay_log("限制: 最大房间 " + std::to_string(MAX_ROOMS) + ", 最大连接 " +
              std::to_string(MAX_CONNECTIONS) + ", 单次传输上限 " +
              std::to_string(RELAY_MAX_DATA / (1024*1024)) + " MB");

    RelayState st;
    while (g_relay_running.load()) {
        // 使用 select 设置 accept 超时, 以便定期检查 g_running 标志
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_sock, &rfds);
        timeval tv{1, 0};  // 1 秒超时
#ifdef _WIN32
        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
#else
        int sel = ::select(listen_sock + 1, &rfds, nullptr, nullptr, &tv);
#endif
        if (sel == 0) continue;  // 超时, 检查 g_running
        if (sel < 0) {
#ifdef _WIN32
            if (sock_errno() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            relay_log("select 失败, errno=" + std::to_string(sock_errno()));
            continue;
        }

        sockaddr_in client{};
        int clen = sizeof(client);
        socket_t conn = ::accept(listen_sock,
                                 reinterpret_cast<sockaddr*>(&client), &clen);
        if (conn == INVALID_SOCK) {
            int e = sock_errno();
#ifdef _WIN32
            if (e == WSAEINTR) continue;
#endif
            relay_log("accept 失败, errno=" + std::to_string(e));
            continue;
        }

        // 检查并发连接数限制
        int current = st.active_connections.fetch_add(1);
        if (current >= MAX_CONNECTIONS) {
            st.active_connections.fetch_sub(1);
            relay_log("连接数已达上限 " + std::to_string(MAX_CONNECTIONS) + ", 拒绝新连接");
            send_line(conn, "ERROR server busy");
            close_socket(conn);
            continue;
        }

        // 记录连接来源 IP (便于排查异常连接)
        char ip_buf[64] = {0};
        ::inet_ntop(AF_INET, &client.sin_addr, ip_buf, sizeof(ip_buf));
        relay_log(std::string("新连接 from ") + ip_buf + ":" + std::to_string(::ntohs(client.sin_port)) +
                  " (当前连接数: " + std::to_string(current + 1) + ")");
        std::thread(conn_thread, &st, conn).detach();
    }

    // 优雅退出: 关闭监听 socket
    relay_log("中继服务器正在关闭...");
    close_socket(listen_sock);
    relay_log("中继服务器已停止");
    return 0;
}

} // namespace ft
