#ifndef RELAY_H
#define RELAY_H

#include <cstdint>
#include <functional>
#include <string>
#include "file_transfer.h"  // 复用 ProgressCallback / CANCELED

namespace ft {

// 中继服务器默认监听端口
constexpr unsigned short DEFAULT_RELAY_PORT = 9091;
// 房间码长度 (6 位字母数字, 36^6 ≈ 22 亿种组合, 抗暴力枚举)
constexpr int ROOM_CODE_LEN = 6;
// 最大同时在线房间数 (防止资源耗尽攻击)
constexpr int MAX_ROOMS = 1000;
// 最大并发连接数 (防止线程耗尽攻击)
constexpr int MAX_CONNECTIONS = 2000;
// 中继转发单次最大数据量 (10GB, 防止带宽滥用)
constexpr uint64_t RELAY_MAX_DATA = 10ULL * 1024 * 1024 * 1024;
// 中继转发超时 (30 分钟, 防止连接永久挂起)
constexpr int RELAY_TIMEOUT_MS = 30 * 60 * 1000;
// 中继 recv_line 超时 (60 秒)
constexpr int RELAY_RECV_TIMEOUT_MS = 60 * 1000;

// =================================================================
// 中继协议 (文本控制行 + 二进制透传)
// =================================================================
//   Sender  -> Relay : "CREATE\n"
//   Relay   -> Sender: "CODE ABC123\n"    (6 位字母数字房间码)
//   Receiver-> Relay : "JOIN ABC123\n"
//   Relay   -> Receiver:"OK\n"            (房间存在, 已配对)
//   Relay   -> Sender : "PEER\n"          (接收方已就绪, 可开始传输)
//   之后 Relay 透传字节流: Sender -> Receiver, 直至 Sender 关闭连接
//
// 文件传输层沿用 PacketHeader 协议 (见 file_transfer.h):
//   | magic(4) | filename_len(4) | file_size(8) | filename(N) | file_data |
// =================================================================

// 启动中继服务器 (阻塞调用, 内部为每个连接创建线程)
// port : 监听端口
// 返回 0 表示正常退出 (目前不会返回, 除非启动失败)
int run_relay_server(unsigned short port);

// 中继发送模式: 连接 relay_host:relay_port, 创建房间, 等待接收方加入后传输文件
// cb 用于报告进度与房间码 (房间码以 "[房间码] XXXXX" 形式由 cb 传回 UI)
// 返回 0 表示成功, CANCELED 表示取消, 其他非 0 表示错误
int relay_send_file(const std::string& relay_host, unsigned short relay_port,
                    const std::string& filepath, ProgressCallback cb = nullptr);

// 中继接收模式: 连接 relay_host:relay_port, 使用 room_code 加入房间, 接收文件
// 返回 0 表示成功, CANCELED 表示取消, 其他非 0 表示错误
int relay_recv_file(const std::string& relay_host, unsigned short relay_port,
                    const std::string& room_code, const std::string& output_dir,
                    ProgressCallback cb = nullptr);

} // namespace ft

#endif // RELAY_H
