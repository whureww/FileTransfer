#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace ft {

// 跨平台 socket 类型 (需在 socket_util.h 之前声明, 供头文件内联函数使用)
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
#endif

// 协议魔数，用于校验数据包合法性
constexpr char MAGIC[4] = {'F', 'T', '0', '1'};
// 当前协议版本 (用于兼容性校验, 未来升级递增)
constexpr uint8_t PROTOCOL_VERSION = 1;
// 默认传输缓冲区大小 (1MB)
constexpr std::size_t BUFFER_SIZE = 1 << 20;
// 默认监听端口
constexpr unsigned short DEFAULT_PORT = 9090;
// UDP 局域网发现端口 (固定, 与 TCP 传输端口独立)
constexpr unsigned short DISCOVERY_PORT = 9090;
// 用户取消传输的错误码
constexpr int CANCELED = 200;

// ===== 错误码 (语义化, 替代魔法数字) =====
enum ErrorCode : int {
    OK              = 0,    // 成功
    ERR_OPEN_FILE   = 1,    // 无法打开文件
    ERR_FILE_SIZE   = 2,    // 无法获取文件大小
    ERR_SOCKET      = 3,    // 创建 socket 失败
    ERR_CONNECT     = 4,    // 连接失败
    ERR_BIND        = 5,    // 绑定端口失败
    ERR_LISTEN      = 6,    // 监听失败
    ERR_ACCEPT      = 17,   // accept 失败
    ERR_RECV_HDR    = 7,    // 接收头部失败
    ERR_BAD_MAGIC   = 8,    // 协议魔数/版本不匹配
    ERR_BAD_NAME    = 9,    // 文件名长度异常
    ERR_RECV_NAME   = 10,   // 接收文件名失败
    ERR_CREATE_FILE = 11,   // 创建输出文件失败
    ERR_SEND_HDR    = 12,   // 发送头部失败
    ERR_RECV_DATA   = 13,   // 接收数据失败
    ERR_WRITE_FILE  = 14,   // 写入文件失败
    ERR_SEND_DATA   = 15,   // 发送数据失败
    ERR_READ_FILE   = 16,   // 读取文件失败
    ERR_SEND_NAME   = 18,   // 发送文件名失败
    ERR_RELAY_LINE  = 20,   // 中继协议: 读取/发送文本行失败
    ERR_RELAY_CODE  = 21,   // 中继协议: 房间码格式错误
    ERR_RELAY_ROOM  = 22,   // 中继协议: 房间不存在/已满
    ERR_RELAY_PEER  = 23,   // 中继协议: 对端异常断开
};

// 错误码转可读文本 (用于 UI 显示和日志)
std::string error_string(int code);

#pragma pack(push, 1)
// 文件传输协议头部 (16 字节, 保持向后布局大小不变)
// | magic(4) | version(1) | flags(1) | filename_len(2) | file_size(8) | filename(N) | file_data(...) |
//   version    : 协议版本, 当前 = 1
//   flags      : 预留标志位 (0 = 无加密/无压缩), 未来可用于 AES 加密、zstd 压缩等
//   filename_len: uint16 (最大 65535, 足够覆盖正常文件名)
struct PacketHeader {
    char magic[4];
    uint8_t version;
    uint8_t flags;
    uint16_t filename_len;
    uint64_t file_size;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be 16 bytes");

// ===== 数据传输帧协议 (二进制, 用于客户端间/客户端与中继间的数据传输阶段) =====
// 帧结构: [1字节类型][4字节长度][payload]
// 类型:
//   0x01 FRAME_DATA   - 数据帧, payload = 文件数据
//   0x02 FRAME_CANCEL - 取消帧, 无 payload, 表示发送方/接收方主动取消
//   0x03 FRAME_DONE   - 完成帧, 无 payload, 表示数据传输完毕
constexpr uint8_t FRAME_DATA   = 0x01;
constexpr uint8_t FRAME_CANCEL = 0x02;
constexpr uint8_t FRAME_DONE   = 0x03;

// 写一个帧到 socket (帧头 + payload)
// 返回 true 表示成功, false 表示连接断开
bool write_frame(socket_t sock, uint8_t type, const char* data, uint32_t len);

// 从 socket 读取一个完整帧
// type_out: 输出帧类型
// data_out: 输出 payload 数据 (由调用者释放)
// len_out: 输出 payload 长度
// 返回: true=成功读取帧, false=连接断开/出错
//   type_out == FRAME_CANCEL 表示对端主动取消
//   type_out == FRAME_DONE   表示对端发送完数据
bool read_frame(socket_t sock, uint8_t& type_out,
                std::vector<char>& data_out, uint32_t& len_out);

// 进度回调函数
//   done  : 已传输字节数
//   total : 总字节数 (total=0 表示仅状态消息, 无进度更新)
//   msg   : 状态/错误文本
//   返回 false 表示用户请求取消传输
using ProgressCallback = std::function<bool(uint64_t done, uint64_t total,
                                            const std::string& msg)>;

// 跨平台 socket 初始化/清理 (Windows 需要 WSAStartup)
bool init_network();
void cleanup_network();

// 发送模式: 连接到 ip:port, 将 filepath 发送到服务端
// cb 为进度回调 (可为 nullptr, 此时输出到控制台)
// 返回 0 表示成功, CANCELED 表示取消, 其他非 0 表示错误
int send_file(const std::string& ip, unsigned short port,
              const std::string& filepath, ProgressCallback cb = nullptr);

// 接收模式: 在 port 上监听, 接收一个文件并保存到 output_dir
// cb 为进度回调 (可为 nullptr, 此时输出到控制台)
// 返回 0 表示成功, CANCELED 表示取消, 其他非 0 表示错误
int recv_file(unsigned short port, const std::string& output_dir,
              ProgressCallback cb = nullptr);

// 二维码 HTTP 直连模式: 服务端绑定端口, 等待客户端连接后发送文件
// 返回 0 成功, CANCELED 取消, 其他错误码
int serve_file(unsigned short port, const std::string& filepath,
               ProgressCallback cb = nullptr);

// 二维码 HTTP 直连模式: 客户端连接到远端 IP:port, 接收文件并保存
// 返回 0 成功, CANCELED 取消, 其他错误码
int connect_recv(const std::string& ip, unsigned short port,
                 const std::string& output_dir, ProgressCallback cb = nullptr);

// ===== 局域网自动发现 (UDP 广播) =====
// 发现协议:
//   发送端 → 255.255.255.255:DISCOVERY_PORT (UDP): "FT_DISCOVER\n"
//   接收端 → 发送端 (UDP): "FT_HERE <tcp_port>\n"

// 发送端调用: 广播发现请求, 收集局域网内在线的接收端
//   tcp_port  : 期望的接收端 TCP 端口 (只收集此端口的响应)
//   timeout_ms: 等待响应超时 (毫秒)
// 返回发现的接收端列表 (ip, tcp_port)
std::vector<std::pair<std::string, unsigned short>>
discover_peers(unsigned short tcp_port, int timeout_ms = 1500);

// 接收端调用: 启动 UDP 发现响应线程
//   tcp_port: 接收端的 TCP 监听端口, 会包含在响应中
//   running : 原子标志, 为 false 时线程退出 (调用者持有, 设为 false 即可停止)
// 返回运行中的线程对象, 调用者需 join
std::thread start_discovery_responder(unsigned short tcp_port,
                                      std::atomic<bool>& running);

} // namespace ft

#endif // FILE_TRANSFER_H
