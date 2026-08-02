package com.filetransfer.protocol

import kotlinx.cinterop.*
import platform.Foundation.*

// iOS 实现: 通过 C 函数指针调用编译为 framework 的 C++ 库
// 实际项目需配合 Swift Bridge (见 iosApp/)
actual object NativeBridge {
    actual fun initNetwork(): Boolean {
        // iOS 无需 WSAStartup, 直接返回 true
        return true
    }

    actual fun cleanupNetwork() {
        // iOS 无需清理
    }

    actual fun errorString(code: Int): String {
        // 使用 C++ 导出的错误码函数 (通过 Swift Bridge)
        return "Error code: $code"
    }

    actual fun relaySendFile(
        host: String, port: Int, filePath: String, callback: ProgressCallback
    ): Int {
        // iOS 通过 Swift Bridge 调用 C++ 函数
        // 实际实现需在 Swift 侧调用 ft_relay_send_file_ios()
        // 这里预留接口, 具体绑定在 Swift Bridge 文件中完成
        return -1
    }

    actual fun relayRecvFile(
        host: String, port: Int, roomCode: String, saveDir: String, callback: ProgressCallback
    ): Int {
        return -1
    }
}
