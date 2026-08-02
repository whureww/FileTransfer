package com.filetransfer.protocol

// Android 实现: 通过 JNI 调用 C++ 核心库 (libfiletransfer_native.so)
actual object NativeBridge {
    init {
        System.loadLibrary("filetransfer_native")
    }

    // JNI 声明 (对应 jni_bridge.cpp 中的导出函数)
    // object 类的 external 方法需要 jobject thiz 参数 (与 C++ JNI 签名一致)
    private external fun initNetworkNative(): Boolean
    private external fun cleanupNetworkNative()
    private external fun errorStringNative(code: Int): String
    private external fun getRelayAddrNative(): String
    private external fun getLocalIpsNative(): String

    private external fun sendFileNative(
        ip: String, port: Int, filePath: String, callback: ProgressCallback
    ): Int
    private external fun recvFileNative(
        port: Int, saveDir: String, callback: ProgressCallback
    ): Int
    private external fun relaySendFileNative(
        host: String, port: Int, filePath: String, callback: ProgressCallback
    ): Int
    private external fun relayRecvFileNative(
        host: String, port: Int, roomCode: String, saveDir: String, callback: ProgressCallback
    ): Int

    actual fun initNetwork(): Boolean = initNetworkNative()
    actual fun cleanupNetwork() = cleanupNetworkNative()
    actual fun errorString(code: Int): String = errorStringNative(code)
    actual fun getRelayAddr(): String = getRelayAddrNative()
    actual fun getLocalIps(): String = getLocalIpsNative()

    actual fun sendFile(ip: String, port: Int, filePath: String, callback: ProgressCallback): Int =
        sendFileNative(ip, port, filePath, callback)

    actual fun recvFile(port: Int, saveDir: String, callback: ProgressCallback): Int =
        recvFileNative(port, saveDir, callback)

    actual fun relaySendFile(host: String, port: Int, filePath: String, callback: ProgressCallback): Int =
        relaySendFileNative(host, port, filePath, callback)

    actual fun relayRecvFile(
        host: String, port: Int, roomCode: String, saveDir: String, callback: ProgressCallback
    ): Int = relayRecvFileNative(host, port, roomCode, saveDir, callback)
}
