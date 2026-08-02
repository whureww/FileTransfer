package com.filetransfer.protocol

// 进度回调接口 (KMP 侧定义, Android 通过 JNI 反射调用, iOS 通过 C 函数指针)
interface ProgressCallback {
    // 返回 false 表示用户请求取消传输
    fun onProgress(done: Long, total: Long, message: String): Boolean
}

// C++ 核心库桥接: expect 声明, 各平台提供 actual 实现
expect object NativeBridge {
    // 初始化网络
    fun initNetwork(): Boolean

    // 清理网络资源
    fun cleanupNetwork()

    // 错误码转可读文本
    fun errorString(code: Int): String

    // ===== 局域网直连 =====

    // 局域网发送文件 (ip 为空时自动发现接收端)
    // 返回 0=成功, 200=取消, 其他=错误码
    fun sendFile(
        ip: String,
        port: Int,
        filePath: String,
        callback: ProgressCallback
    ): Int

    // 局域网接收文件 (内部启动 UDP 发现响应线程)
    // 返回 0=成功, 200=取消, 其他=错误码
    fun recvFile(
        port: Int,
        saveDir: String,
        callback: ProgressCallback
    ): Int

    // ===== 房间码中继 (跨局域网) =====

    // 中继发送文件 (创建房间, 返回房间码)
    // 返回 0=成功, 200=取消, 其他=错误码
    fun relaySendFile(
        host: String,
        port: Int,
        filePath: String,
        callback: ProgressCallback
    ): Int

    // 中继接收文件 (输入房间码加入房间)
    // 返回 0=成功, 200=取消, 其他=错误码
    fun relayRecvFile(
        host: String,
        port: Int,
        roomCode: String,
        saveDir: String,
        callback: ProgressCallback
    ): Int

    // ===== 配置查询 =====

    // 获取默认中继服务器地址 "host:port" (从加密配置解密)
    fun getRelayAddr(): String

    // 获取本机所有 IPv4 地址 (换行分隔)
    fun getLocalIps(): String
}
