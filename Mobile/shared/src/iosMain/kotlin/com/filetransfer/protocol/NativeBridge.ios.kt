package com.filetransfer.protocol

import kotlinx.cinterop.*
import platform.Foundation.*

// iOS 实现: 通过 C 函数指针调用编译为 framework 的 C++ 库
// 实际项目需配合 Swift Bridge (见 iosApp/)
actual object NativeBridge {
    actual fun initNetwork(): Boolean = true
    actual fun cleanupNetwork() {}
    actual fun errorString(code: Int): String = "Error code: $code"
    actual fun getRelayAddr(): String = ""   // iOS 通过 Swift Bridge 获取
    actual fun getLocalIps(): String = ""

    actual fun sendFile(ip: String, port: Int, filePath: String, callback: ProgressCallback): Int = -1
    actual fun recvFile(port: Int, saveDir: String, callback: ProgressCallback): Int = -1

    actual fun relaySendFile(
        host: String, port: Int, filePath: String, callback: ProgressCallback
    ): Int = -1

    actual fun relayRecvFile(
        host: String, port: Int, roomCode: String, saveDir: String, callback: ProgressCallback
    ): Int = -1
}
