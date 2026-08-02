package com.filetransfer.protocol

// 进度回调接口 (KMP 侧定义, Android 通过 JNI 反射调用, iOS 通过 C 函数指针)
interface ProgressCallback {
    // 返回 false 表示用户请求取消传输
    fun onProgress(done: Long, total: Long, message: String): Boolean
}

// C++ 核心库桥接: expect 声明, 各平台提供 actual 实现
expect object NativeBridge {
    // 初始化网络 (Windows 需要 WSAStartup, Android/iOS 无需操作)
    fun initNetwork(): Boolean

    // 清理网络资源
    fun cleanupNetwork()

    // 错误码转可读文本
    fun errorString(code: Int): String

    // 中继发送文件
    // 返回 0=成功, 200=取消, 其他=错误码
    fun relaySendFile(
        host: String,
        port: Int,
        filePath: String,
        callback: ProgressCallback
    ): Int

    // 中继接收文件
    // 返回 0=成功, 200=取消, 其他=错误码
    fun relayRecvFile(
        host: String,
        port: Int,
        roomCode: String,
        saveDir: String,
        callback: ProgressCallback
    ): Int
}
