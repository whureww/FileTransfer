# FileTransfer

跨平台文件传输工具，支持局域网直连和中继服务器转发两种模式。

**当前版本：v0.0.6**

## 功能特性

- **局域网直连**：通过 UDP 广播自动发现同局域网内的接收端，TCP 直连传输
- **中继转发**：通过公网中继服务器跨局域网传输，使用 6 位房间码配对
- **自定义中继服务器**：发送端可设置自定义中继服务器 IP 和端口，接收端自动沿用
- **自定义 GUI**：原生 Win32 GUI 客户端，支持拖拽、进度条、关闭确认对话框
- **大文件支持**：协议使用 `uint64_t` 文件大小字段，理论支持 16 EB
- **单实例运行**：每台设备仅允许运行一个客户端/中继服务器实例
- **安全传输**：文件名过滤防目录穿越、协议魔数/版本校验、磁盘空间预检
- **中继服务器保护**：连接数限制、房间数限制、传输超时、数据量上限
- **双向取消通知**：发送方取消时接收方显示"发送方已取消传输"，接收方取消时发送方显示"接收方已取消传输"
- **UI 自动清理**：传输完成/取消/失败后自动清空房间码、文件路径、进度条，方便立即开始新传输

## 支持传输的文件类型

本程序以**原始字节流**方式传输文件，不检查、不修改文件内容，因此**支持所有文件类型**。包括但不限于：

### 系统与镜像
| 类型 | 扩展名 | 说明 |
|------|--------|------|
| 光盘镜像 | `.iso` `.img` `.bin` `.cue` `.dmg` | 各类操作系统安装镜像 (CentOS, Ubuntu, Windows 等) |
| 虚拟磁盘 | `.vmdk` `.vhd` `.vhdx` `.qcow2` | VMware / Hyper-V / QEMU 虚拟机磁盘 |
| 系统备份 | `.gho` `.wim` `.esd` `.tib` | Ghost / Windows 备份 / Acronis 镜像 |
| 引导文件 | `.efi` `.img` `.boot` | UEFI 引导、启动盘镜像 |

### 压缩与归档
| 类型 | 扩展名 | 说明 |
|------|--------|------|
| 通用压缩 | `.zip` `.rar` `.7z` `.tar` `.gz` `.bz2` `.xz` `.zst` | 各类压缩格式 |
| 分卷压缩 | `.part1` `.z01` `.001` | 分卷包各部分均可独立传输 |
| 归档 | `.tar` `.cpio` `.ar` | Unix 归档格式 |

### 文档与办公
| 类型 | 扩展名 | 说明 |
|------|--------|------|
| Office | `.doc` `.docx` `.xls` `.xlsx` `.ppt` `.pptx` `.odt` `.ods` `.odp` | Microsoft Office / WPS / LibreOffice |
| PDF | `.pdf` | PDF 文档 |
| 电子书 | `.epub` `.mobi` `.azw3` `.djvu` | 各类电子书格式 |
| 文本 | `.txt` `.md` `.rtf` `.csv` `.log` `.json` `.xml` `.yaml` | 纯文本与标记语言 |
| 代码 | `.c` `.cpp` `.h` `.py` `.java` `.js` `.ts` `.go` `.rs` `.sh` `.bat` `.ps1` | 各编程语言源码 |

### 媒体文件
| 类型 | 扩展名 | 说明 |
|------|--------|------|
| 视频 | `.mp4` `.mkv` `.avi` `.mov` `.flv` `.wmv` `.webm` `.m4v` `.ts` | 各类视频格式 |
| 音频 | `.mp3` `.flac` `.wav` `.aac` `.ogg` `.m4a` `.opus` `.wma` | 各类音频格式 |
| 图片 | `.jpg` `.jpeg` `.png` `.gif` `.bmp` `.webp` `.tiff` `.svg` `.heic` `.raw` | 各类图片格式 |
| 字幕 | `.srt` `.ass` `.ssa` `.sub` `.vtt` | 字幕文件 |

### 程序与二进制
| 类型 | 扩展名 | 说明 |
|------|--------|------|
| Windows 程序 | `.exe` `.dll` `.msi` `.sys` `.ocx` `.cpl` | 可执行文件与动态库 |
| 安装包 | `.exe` `.msi` `.7z` `.zip` | 各类安装程序 |
| 脚本 | `.bat` `.cmd` `.ps1` `.sh` `.py` `.vbs` | 脚本文件 |
| 数据库 | `.db` `.sqlite` `.mdb` `.sql` `.bak` | 数据库文件与备份 |

### 开发与工程
| 类型 | 扩展名 | 说明 |
|------|--------|------|
| 镜像/固件 | `.bin` `.hex` `.fw` `.dfu` | 嵌入式固件、刷机包 |
| 开发工具 | `.jar` `.war` `.class` `.apk` `.ipa` `.deb` `.rpm` `.appimage` | 跨平台应用包 |
| 容器/虚拟化 | `.dockerfile` `.ova` `.ovf` `.box` | 容器与虚拟机镜像 |
| CAD/设计 | `.dwg` `.dxf` `.psd` `.ai` `.sketch` `.fig` | 工程图纸与设计文件 |

### 限制说明

- **文件大小**：理论最大 16 EB（`uint64_t`），实际受磁盘空间和网络带宽限制
- **文件名长度**：最大 65535 字节（`uint16_t`）
- **文件名过滤**：自动去除路径分隔符 (`/` `\`)、Windows 非法字符 (`:` `*` `?` `"` `<` `>` `|`)、控制字符，并拦截保留设备名 (CON、PRN、NUL、AUX、COM1-9、LPT1-9、CONIN$、CONOUT$)
- **路径编码**：支持 UTF-8 编码的文件路径，包括中文、日文、韩文、emoji 等非 ASCII 字符
- **传输模式**：原始字节流，不进行任何编码转换、压缩或加密，保证文件二进制一致性

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
| 单实例限制 | Named Mutex 确保每台设备仅运行一个实例 |

## 技术规格

- **语言**：C++17
- **GUI**：Win32 API (Owner-draw controls, Comctl32)
- **网络**：POSIX socket / Winsock2
- **线程**：std::thread (每连接一线程模型)
- **构建**：CMake 3.15+
- **安装器**：Inno Setup 7
- **运行时依赖**：MSVCP140.dll, VCRUNTIME140.dll, VCRUNTIME140_1.dll (动态链接 CRT)

## 更新日志

### v0.0.6 (2026-08-02)

**安全漏洞修复**

- 修复 `read_frame` 无帧长度上限导致远程内存耗尽 DoS 漏洞：添加 `MAX_FRAME_PAYLOAD` 校验，拒绝超过 1MB 的帧
- 修复接收方未校验 `received > file_size` 导致磁盘填满攻击：接收数据前检查是否超出声明的文件大小
- 中继服务器新增 JOIN 速率限制：每 IP 60 秒内最多 5 次失败，防止房间码暴力枚举
- 修复中继服务器线程 detach 导致关闭时潜在 Use-After-Free：改为线程池管理，优雅退出时 join 所有工作线程

### v0.0.5 (2026-08-01)

**双向取消通知 & UI 自动清理**

- 新增二进制帧传输协议：`FRAME_DATA`（数据帧）、`FRAME_CANCEL`（取消帧）、`FRAME_DONE`（完成帧），统一 LAN 直连与中继两种模式
- 发送方取消传输时，主动发送 CANCEL 帧通知接收方，接收方显示"发送方已取消传输"而非"连接断开"
- 接收方取消传输时，发送方在后续数据写入时检测到连接错误，正确处理取消场景
- 中继模式：发送方取消时通过中继转发 CANCEL 帧，中继方日志显示"发送方主动取消, 清理房间"
- 传输完成/取消/失败后，UI 自动清理房间码、文件路径、进度条归零，方便立即发起新传输
- 修复"接收端报错而非显示发送方取消提示"的问题

### v0.0.4 (2026-08-01)

- 修复 Windows 下含中文/非 ASCII 字符路径的文件无法打开的问题（如 `D:\系统镜像\CentOS.iso`）
- 新增 `utf8_to_wpath` 宽字符路径转换，覆盖 `std::ifstream`、`std::ofstream`、`std::filesystem` 全部文件操作
- 修复 `normalize_dir` 和 `unique_filepath` 在非 ASCII 目录下创建目录/生成路径失败的问题
- README 新增「支持的文件类型」详细说明

### v0.0.3 (2026-08-01)

**安全与健壮性修复（13 项）**

高优先级：
- 修复 Windows 下含中文/非 ASCII 字符的文件路径无法打开的问题：`std::ifstream`/`std::ofstream`/`std::filesystem` 在 MSVC 中不支持 UTF-8，新增 `utf8_to_wpath` 宽字符转换
- 修复中继服务器 `handle_receiver` 的 Use-After-Free 崩溃：sender 线程超时清理 `delete room` 后，receiver 线程回滚时仍访问 room 指针
- 全面对齐 ErrorCode 枚举与实际返回值（`file_transfer.cpp` × 2、`relay_client.cpp`，约 50 处），确保 `error_string()` 输出正确
- 修复 LAN 接收端 `accept()` 阻塞导致取消按钮无效：改用 `select()` 200ms 轮询
- 修复客户端单实例 Mutex 未 `CloseHandle` 的句柄泄漏

中优先级：
- 自定义中继服务器设置持久化到注册表，重启后保留配置
- 中继服务器 JOIN 时校验房间码字符集（A-Z 排除 I/O，2-9 排除 0/1），与生成端一致
- 中继发送方等待 PEER 期间支持取消（`select` 200ms 轮询替代阻塞 `recv_line`）
- `sanitize_filename` 补全 Windows 保留设备名过滤（COM10+、LPT10+、CONIN$、CONOUT$）

低优先级：
- `post_progress` 节流时间戳从 `static` 改为 `AppContext` 成员，消除多线程隐患
- 修复 `.gitignore` 误排除 `installer/*.iss` 模板文件
- 添加 `installer/Client.iss` 和 `installer/Relay.iss` 到版本控制

### v0.0.2 (2026-08-01)

- 客户端高级设置：自定义中继服务器 IP 和端口，接收端自动沿用
- 单实例检测：每台设备仅允许运行一个客户端/中继服务器
- 中继服务器增强：连接数/房间数限制、传输超时、数据量上限、线程安全日志
- 文件安全：磁盘空间预检、唯一文件路径（不覆盖同名文件）
- 协议增强：PacketHeader 增加 version 字段、ErrorCode 枚举替代魔法数字
- 进度回调节流（100ms），防止大文件传输时 UI 卡顿
- 关闭窗口对话框（最小化到托盘 / 退出 / 记住选择）
- Inno Setup 安装器打包

### v0.0.1 (2026-08-01)

- 初始版本
- 局域网直连文件传输（UDP 自动发现 + TCP 传输）
- 中继服务器转发（6 位房间码配对）
- Win32 GUI 客户端（进度条、日志、拖拽选择文件）
- 跨平台编译支持（CMake + MSVC/GCC/Clang）

## 许可证

本项目仅供学习和个人使用。
