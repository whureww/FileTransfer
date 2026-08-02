package com.filetransfer.protocol

// Android 实现: 通过 JNI 调用 C++ 核心库 (libfiletransfer_native.so)
actual object NativeBridge {
    init {
        System.loadLibrary("filetransfer_native")
    }

    actual fun initNetwork(): Boolean = initNetworkNative()
    actual fun cleanupNetwork() = cleanupNetworkNative()
    actual fun errorString(code: Int): String = errorStringNative(code)

    actual fun relaySendFile(
        host: String, port: Int, filePath: String, callback: ProgressCallback
    ): Int = relaySendFileNative(host, port, filePath, callback)

    actual fun relayRecvFile(
        host: String, port: Int, roomCode: String, saveDir: String, callback: ProgressCallback
    ): Int = relayRecvFileNative(host, port, roomCode, saveDir, callback)

    // JNI 声明 (对应 jni_bridge.cpp 中的导出函数)
    @JvmStatic private external fun initNetworkNative(): Boolean
    @JvmStatic private external fun cleanupNetworkNative()
    @JvmStatic private external fun errorStringNative(code: Int): String
    @JvmStatic private external fun relaySendFileNative(
        host: String, port: Int, filePath: String, callback: ProgressCallback
    ): Int
    @JvmStatic private external fun relayRecvFileNative(
        host: String, port: Int, roomCode: String, saveDir: String, callback: ProgressCallback
    ): Int
}
