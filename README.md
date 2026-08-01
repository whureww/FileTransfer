# FileTransfer

跨平台文件传输工具，支持局域网直连和中继服务器转发两种模式。

## 功能特性

- **局域网直连**：通过 UDP 广播自动发现同局域网内的接收端，TCP 直连传输
- **中继转发**：通过公网中继服务器跨局域网传输，使用 6 位房间码配对
- **自定义 GUI**：原生 Win32 GUI 客户端，支持拖拽、进度条、关闭确认对话框
- **大文件支持**：协议使用 `uint64_t` 文件大小字段，理论支持 16 EB
- **安全传输**：文件名过滤防目录穿越、协议魔数/版本校验、磁盘空间预检
- **中继服务器保护**：连接数限制、房间数限制、传输超时、数据量上限

## 项目结构

```
FileTransfer/
├── Client/              # GUI 客户端
│   ├── src/
│   │   ├── file_transfer.h/cpp   # 文件传输核心 (send_file / recv_file)
│   │   ├── socket_util.h/cpp     # 跨平台 socket 辅助函数
│   │   ├── relay.h               # 中继协议定义
│   │   ├── relay_client.cpp      # 中继客户端 (发送方/接收方)
│   │   ├── secret.h/cpp          # 中继地址 (XOR 混淆) + 本机 IP 获取
│   │   └── gui_main.cpp          # Win32 GUI 主程序
│   ├── CMakeLists.txt
│   └── build.bat
├── Relay/               # 中继服务器 (控制台程序)
│   ├── src/
│   │   ├── file_transfer.h/cpp   # 共享协议定义
│   │   ├── socket_util.h/cpp     # 共享 socket 辅助函数
│   │   ├── relay.h               # 中继协议定义
│   │   ├── relay_server.cpp      # 中继服务器实现
│   │   └── relay_main.cpp        # 控制台入口
│   ├── CMakeLists.txt
│   └── build.bat
├── assets/              # 图标资源
├── installer/           # Inno Setup 安装脚本
└── .gitignore
```

## 传输协议

### 文件传输协议 (TCP)

```
| magic(4) | version(1) | flags(1) | filename_len(2) | file_size(8) | filename(N) | file_data(...) |
```

- `magic`：`"FT01"`，固定 4 字节，用于校验数据包合法性
- `version`：协议版本号，当前为 `1`
- `flags`：预留标志位（0 = 无加密/无压缩）
- `filename_len`：文件名长度（最大 65535 字节）
- `file_size`：文件大小（`uint64_t`，理论最大 16 EB）

### 中继协议 (文本控制行 + 二进制透传)

```
Sender  -> Relay : "CREATE\n"
Relay   -> Sender: "CODE ABC123\n"     (6 位房间码)
Receiver-> Relay : "JOIN ABC123\n"
Relay   -> Receiver: "OK\n"            (房间存在, 已配对)
Relay   -> Sender : "PEER\n"           (接收方已就绪)
之后 Relay 透传字节流: Sender -> Receiver
```

### 局域网发现协议 (UDP)

```
发送端 -> 255.255.255.255:9090 (UDP): "FT_DISCOVER\n"
接收端 -> 发送端 (UDP): "FT_HERE <tcp_port>\n"
```

## 构建方法

### 前置要求

- **编译器**：支持 C++17 的编译器（MSVC 2019+ / GCC 9+ / Clang 10+）
- **构建工具**：CMake 3.15+
- **Windows GUI 客户端**：需要 Windows SDK（comctl32, comdlg32 等）
- **安装器**（可选）：Inno Setup 6/7

### Windows 构建 (MSVC)

```powershell
# 客户端
cd Client
.\build.bat

# 中继服务器
cd Relay
.\build.bat
```

构建产物：
- `Client/build/FileTransfer.exe` — GUI 客户端
- `Client/build/installer/FileTransfer_Client_Setup.exe` — 客户端安装器
- `Relay/build/FileTransferRelay.exe` — 中继服务器
- `Relay/build/installer/FileTransfer_Relay_Setup.exe` — 中继服务器安装器

### Linux/macOS 构建 (GCC/Clang)

```bash
# 中继服务器
cd Relay
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

> 注意：GUI 客户端仅支持 Windows，中继服务器可跨平台编译。

## 使用方法

### 局域网直连

1. 接收方在客户端选择「接收文件」，选择保存目录
2. 发送方在客户端选择「发送文件」，点击「扫描」发现局域网内的接收端
3. 选择目标 IP，点击发送

### 中继转发

1. 在公网服务器上运行 `FileTransferRelay.exe [port]`（默认端口 9091）
2. 发送方选择「中继发送」，选择文件，获得 6 位房间码
3. 接收方选择「中继接收」，输入房间码，等待文件传输

### 中继服务器参数

```
FileTransferRelay.exe [port]

默认端口: 9091
最大房间数: 1000
最大连接数: 2000
单次传输上限: 10 GB
传输超时: 30 分钟
Ctrl+C 优雅退出
```

## 安全机制

| 机制 | 说明 |
|------|------|
| 文件名过滤 | 移除路径分隔符、控制字符、Windows 非法字符，防目录穿越 |
| 保留设备名防护 | 自动重命名 CON、PRN、NUL、AUX、COM1-9、LPT1-9 |
| 协议校验 | 魔数 + 版本号双重校验，拒绝不兼容数据包 |
| 磁盘空间预检 | 接收前检查磁盘剩余空间，避免传输中途失败 |
| 文件名长度限制 | 限制为 4096 字节，防止缓冲区溢出 |
| 中继地址混淆 | XOR 加密 + 密钥 mask，防止 strings 扫描 |
| 中继连接限制 | 最大 2000 并发连接，防止线程耗尽 |
| 中继房间限制 | 最大 1000 房间，防止内存耗尽 |
| 中继传输限制 | 单次最大 10GB、30 分钟超时，防止带宽滥用 |
| 中继 recv 超时 | 60 秒首行超时，防止 slowloris 攻击 |
| 线程安全日志 | 使用 `localtime_s`/`localtime_r` + 互斥锁 |
| 同名文件保护 | 自动追加 `(1)`, `(2)` 后缀，不覆盖已有文件 |

## 技术规格

- **语言**：C++17
- **GUI**：Win32 API (Owner-draw controls, Comctl32)
- **网络**：POSIX socket / Winsock2
- **线程**：std::thread (每连接一线程模型)
- **构建**：CMake 3.15+
- **安装器**：Inno Setup 7
- **运行时依赖**：MSVCP140.dll, VCRUNTIME140.dll, VCRUNTIME140_1.dll (动态链接 CRT)

## 许可证

本项目仅供学习和个人使用。
