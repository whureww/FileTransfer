package com.filetransfer.service

import com.filetransfer.model.TransferMode
import com.filetransfer.model.TransferState
import com.filetransfer.protocol.NativeBridge
import com.filetransfer.protocol.ProgressCallback
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

// 传输服务: 管理文件传输的生命周期
// 被各平台 UI 共享, 通过 StateFlow 暴露状态
class TransferService {
    private val _state = MutableStateFlow<TransferState>(TransferState.Idle)
    val state: StateFlow<TransferState> = _state.asStateFlow()

    private var transferJob: Job? = null
    private var canceled = false

    // 中继服务器地址 (UI 层可配置)
    var relayHost: String = "your-relay-server.com"
    var relayPort: Int = 9091

    init {
        NativeBridge.initNetwork()
    }

    // 发送文件
    fun sendFile(filePath: String) {
        if (transferJob?.isActive == true) return

        canceled = false
        transferJob = CoroutineScope(Dispatchers.Default).launch {
            _state.value = TransferState.Connecting

            val callback = object : ProgressCallback {
                override fun onProgress(done: Long, total: Long, message: String): Boolean {
                    // 从消息中解析房间码 (C++ 端格式: "[房间码] XXXXXX")
                    if (message.startsWith("[房间码]")) {
                        val code = message.substringAfter("[房间码]").trim()
                        _state.value = TransferState.WaitingForPeer(code)
                        return !canceled
                    }
                    if (total > 0) {
                        _state.value = TransferState.Transferring(done, total, message)
                    }
                    return !canceled
                }
            }

            val result = NativeBridge.relaySendFile(relayHost, relayPort, filePath, callback)

            _state.value = when {
                canceled -> TransferState.Canceled
                result == 0 -> TransferState.Done
                else -> TransferState.Error(result, NativeBridge.errorString(result))
            }
        }
    }

    // 接收文件
    fun recvFile(roomCode: String, saveDir: String) {
        if (transferJob?.isActive == true) return

        canceled = false
        transferJob = CoroutineScope(Dispatchers.Default).launch {
            _state.value = TransferState.Connecting

            val callback = object : ProgressCallback {
                override fun onProgress(done: Long, total: Long, message: String): Boolean {
                    if (total > 0) {
                        _state.value = TransferState.Transferring(done, total, message)
                    }
                    return !canceled
                }
            }

            val result = NativeBridge.relayRecvFile(relayHost, relayPort, roomCode, saveDir, callback)

            _state.value = when {
                canceled -> TransferState.Canceled
                result == 0 -> TransferState.Done
                else -> TransferState.Error(result, NativeBridge.errorString(result))
            }
        }
    }

    // 取消当前传输
    fun cancel() {
        canceled = true
    }

    // 重置为空闲状态
    fun reset() {
        transferJob?.cancel()
        canceled = false
        _state.value = TransferState.Idle
    }

    // 清理资源
    fun destroy() {
        transferJob?.cancel()
        NativeBridge.cleanupNetwork()
    }
}
