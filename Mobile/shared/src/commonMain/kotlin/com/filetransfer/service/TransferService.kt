package com.filetransfer.service

import com.filetransfer.model.TransferState
import com.filetransfer.protocol.NativeBridge
import com.filetransfer.protocol.ProgressCallback
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

// 传输服务: 管理局域网直连 / 房间码中继两种模式的文件传输
class TransferService {
    private val _state = MutableStateFlow<TransferState>(TransferState.Idle)
    val state: StateFlow<TransferState> = _state.asStateFlow()

    // 日志流 (UI 层观察并追加到日志窗口)
    private val _logs = MutableStateFlow<List<String>>(emptyList())
    val logs: StateFlow<List<String>> = _logs.asStateFlow()

    private var transferJob: Job? = null
    private var canceled = false

    // 局域网直连端口 (默认 9090, 与 PC 端一致)
    var lanPort: Int = 9090

    // 默认保存目录 (接收文件存放位置)
    var saveDir: String = ""

    // 自定义保存目录 (通过系统选择器选择, URI 字符串)
    private val _customSaveDirUri = MutableStateFlow<String?>(null)
    val customSaveDirUri: StateFlow<String?> = _customSaveDirUri.asStateFlow()

    // 中继服务器地址: 默认从 C++ 加密配置读取, 用户可通过高级设置覆盖
    var relayHost: String = ""
        private set
    var relayPort: Int = 0
        private set
    // 是否使用自定义中继服务器 (高级设置)
    var useCustomRelay: Boolean = false
        private set
    private var customRelayHost: String = ""
    private var customRelayPort: Int = 0

    init {
        NativeBridge.initNetwork()
        // 从 C++ 加密配置加载默认中继服务器地址
        loadDefaultRelayAddr()
    }

    private fun loadDefaultRelayAddr() {
        val addr = NativeBridge.getRelayAddr()
        if (addr.isNotEmpty()) {
            val parts = addr.split(":")
            if (parts.size == 2) {
                relayHost = parts[0]
                relayPort = parts[1].toIntOrNull() ?: 9091
            }
        }
        if (relayHost.isEmpty()) {
            relayHost = "127.0.0.1"
            relayPort = 9091
        }
    }

    // 设置自定义中继服务器 (高级设置); 传空则恢复默认
    fun setCustomRelay(host: String?, port: Int?) {
        if (host.isNullOrEmpty() || port == null || port <= 0) {
            useCustomRelay = false
            customRelayHost = ""
            customRelayPort = 0
        } else {
            useCustomRelay = true
            customRelayHost = host
            customRelayPort = port
        }
    }

    // 设置自定义保存目录 (URI 字符串); 传空则清除
    fun setCustomSaveDir(uri: String?) {
        _customSaveDirUri.value = uri
    }

    // 获取当前生效的中继服务器地址
    fun effectiveRelayHost(): String =
        if (useCustomRelay) customRelayHost else relayHost
    fun effectiveRelayPort(): Int =
        if (useCustomRelay) customRelayPort else relayPort

    // ===== 日志 =====
    private fun appendLog(msg: String) {
        _logs.value = _logs.value + msg
    }

    fun clearLogs() {
        _logs.value = emptyList()
    }

    // ===== 自动重置 (Canceled/Done/Error 后回到 Idle, 保留日志) =====
    private fun autoReset(delayMs: Long = 1500) {
        CoroutineScope(Dispatchers.Default).launch {
            delay(delayMs)
            _state.value = TransferState.Idle
        }
    }

    // ===== 局域网直连: 发送 =====
    fun sendFileLan(filePath: String, port: Int = lanPort) {
        if (transferJob?.isActive == true) return
        canceled = false
        clearLogs()
        appendLog("========== 发送文件 (局域网自动发现) ==========")

        transferJob = CoroutineScope(Dispatchers.Default).launch {
            _state.value = TransferState.Connecting

            val callback = object : ProgressCallback {
                override fun onProgress(done: Long, total: Long, message: String): Boolean {
                    if (total > 0) {
                        _state.value = TransferState.Transferring(done, total, message)
                    } else if (message.isNotEmpty()) {
                        appendLog(message)
                    }
                    return !canceled
                }
            }

            // ip 传空字符串 → C++ 端自动发现接收端
            val result = NativeBridge.sendFile("", port, filePath, callback)

            _state.value = when {
                canceled -> { appendLog("[取消] 传输已取消"); TransferState.Canceled }
                result == 0 -> { appendLog("[完成] 文件发送成功"); TransferState.Done }
                else -> {
                    appendLog("[失败] 错误码: $result (${NativeBridge.errorString(result)})")
                    TransferState.Error(result, NativeBridge.errorString(result))
                }
            }
            autoReset()
        }
    }

    // ===== 局域网直连: 接收 =====
    fun recvFileLan(saveDir: String, port: Int = lanPort) {
        if (transferJob?.isActive == true) return
        canceled = false
        clearLogs()
        appendLog("========== 接收文件 (局域网直连) ==========")

        // 显示本机 IP 供发送方参考
        val ips = NativeBridge.getLocalIps()
        if (ips.isNotEmpty()) {
            appendLog("本机 IP 地址:")
            ips.split("\n").forEach { appendLog("  $it:$port") }
        }
        appendLog("已开启局域网自动发现, 等待发送端连接...")

        transferJob = CoroutineScope(Dispatchers.Default).launch {
            _state.value = TransferState.Connecting

            val callback = object : ProgressCallback {
                override fun onProgress(done: Long, total: Long, message: String): Boolean {
                    if (total > 0) {
                        _state.value = TransferState.Transferring(done, total, message)
                    } else if (message.isNotEmpty()) {
                        appendLog(message)
                    }
                    return !canceled
                }
            }

            val result = NativeBridge.recvFile(port, saveDir, callback)

            _state.value = when {
                canceled -> { appendLog("[取消] 传输已取消"); TransferState.Canceled }
                result == 0 -> { appendLog("[完成] 文件接收成功, 保存到: $saveDir"); TransferState.Done }
                else -> {
                    appendLog("[失败] 错误码: $result (${NativeBridge.errorString(result)})")
                    TransferState.Error(result, NativeBridge.errorString(result))
                }
            }
            autoReset()
        }
    }

    // ===== 房间码中继: 发送 (创建房间) =====
    fun sendFileRelay(filePath: String) {
        if (transferJob?.isActive == true) return
        canceled = false
        clearLogs()
        appendLog("========== 中继发送 (创建房间) ==========")

        appendLog("[信息] 正在连接中继服务器...")

        transferJob = CoroutineScope(Dispatchers.Default).launch {
            _state.value = TransferState.Connecting

            val callback = object : ProgressCallback {
                override fun onProgress(done: Long, total: Long, message: String): Boolean {
                    // 从消息中解析房间码 (C++ 端格式: "[房间码] XXXXXX")
                    if (message.startsWith("[房间码]")) {
                        val code = message.substringAfter("[房间码]").trim()
                        _state.value = TransferState.WaitingForPeer(code)
                        appendLog("[房间码] $code  (将此房间码告知接收方)")
                        return !canceled
                    }
                    if (total > 0) {
                        _state.value = TransferState.Transferring(done, total, message)
                    } else if (message.isNotEmpty()) {
                        appendLog(message)
                    }
                    return !canceled
                }
            }

            val result = NativeBridge.relaySendFile(
                effectiveRelayHost(), effectiveRelayPort(), filePath, callback
            )

            _state.value = when {
                canceled -> { appendLog("[取消] 传输已取消"); TransferState.Canceled }
                result == 0 -> { appendLog("[完成] 文件发送成功"); TransferState.Done }
                else -> {
                    appendLog("[失败] 错误码: $result (${NativeBridge.errorString(result)})")
                    TransferState.Error(result, NativeBridge.errorString(result))
                }
            }
            autoReset()
        }
    }

    // ===== 房间码中继: 接收 (加入房间) =====
    fun recvFileRelay(roomCode: String, saveDir: String) {
        if (transferJob?.isActive == true) return
        canceled = false
        clearLogs()
        appendLog("========== 中继接收 (加入房间) ==========")

        appendLog("[信息] 正在连接中继服务器...")
        appendLog("[信息] 房间码: $roomCode")

        transferJob = CoroutineScope(Dispatchers.Default).launch {
            _state.value = TransferState.Connecting

            val callback = object : ProgressCallback {
                override fun onProgress(done: Long, total: Long, message: String): Boolean {
                    if (total > 0) {
                        _state.value = TransferState.Transferring(done, total, message)
                    } else if (message.isNotEmpty()) {
                        appendLog(message)
                    }
                    return !canceled
                }
            }

            val result = NativeBridge.relayRecvFile(
                effectiveRelayHost(), effectiveRelayPort(), roomCode, saveDir, callback
            )

            _state.value = when {
                canceled -> { appendLog("[取消] 传输已取消"); TransferState.Canceled }
                result == 0 -> { appendLog("[完成] 文件接收成功, 保存到: $saveDir"); TransferState.Done }
                else -> {
                    appendLog("[失败] 错误码: $result (${NativeBridge.errorString(result)})")
                    TransferState.Error(result, NativeBridge.errorString(result))
                }
            }
            autoReset()
        }
    }

    // ===== 扫码接收 (解析二维码内容) =====
    // 支持两种格式:
    //   FT1|H|local_ip|port  → HTTP 直连 (二维码发送模式, 无需中继)
    //   FT1|R|relay_host|port|room_code → 房间码中继
    fun recvFileByScan(qrContent: String, saveDir: String): Boolean {
        if (transferJob?.isActive == true) return false

        val parts = qrContent.split("|")
        if (parts.isEmpty() || parts[0] != "FT1") {
            appendLog("[错误] 无效的二维码内容: $qrContent")
            return false
        }

        when (parts.getOrNull(1)) {
            "H" -> {
                // HTTP 直连模式: FT1|H|local_ip|port
                // PC 端作为服务端绑定端口等待手机连接, 手机端作为客户端接收文件
                if (parts.size < 4) {
                    appendLog("[错误] 二维码格式不正确 (缺少 IP 和端口)")
                    return false
                }
                val ip = parts[2]
                val port = parts[3].toIntOrNull() ?: run {
                    appendLog("[错误] 端口号无效: ${parts[3]}")
                    return false
                }

                canceled = false
                clearLogs()
                appendLog("========== 扫码接收 (HTTP 直连) ==========")
                appendLog("[信息] 正在连接 $ip:$port ...")

                transferJob = CoroutineScope(Dispatchers.Default).launch {
                    _state.value = TransferState.Connecting

                    val callback = object : ProgressCallback {
                        override fun onProgress(done: Long, total: Long, message: String): Boolean {
                            if (total > 0) {
                                _state.value = TransferState.Transferring(done, total, message)
                            } else if (message.isNotEmpty()) {
                                appendLog(message)
                            }
                            return !canceled
                        }
                    }

                    // 手机作为客户端, 连接 PC 端接收文件
                    val result = NativeBridge.connectRecv(ip, port, saveDir, callback)

                    _state.value = when {
                        canceled -> { appendLog("[取消] 传输已取消"); TransferState.Canceled }
                        result == 0 -> { appendLog("[完成] 文件接收成功, 保存到: $saveDir"); TransferState.Done }
                        else -> {
                            appendLog("[失败] 错误码: $result (${NativeBridge.errorString(result)})")
                            TransferState.Error(result, NativeBridge.errorString(result))
                        }
                    }
                    autoReset()
                }
                return true
            }

            "R" -> {
                // 中继模式: FT1|R|relay_host|port|room_code
                if (parts.size < 5) {
                    appendLog("[错误] 二维码格式不正确 (缺少中继参数)")
                    return false
                }
                val relayHost = parts[2]
                val relayPort = parts[3].toIntOrNull() ?: run {
                    appendLog("[错误] 中继端口无效: ${parts[3]}")
                    return false
                }
                val roomCode = parts[4].trim().uppercase()
                if (roomCode.length != 6) {
                    appendLog("[错误] 房间码长度不正确: $roomCode")
                    return false
                }

                canceled = false
                clearLogs()
                appendLog("========== 扫码接收 (中继) ==========")
                appendLog("[信息] 正在连接中继服务器...")
                appendLog("[信息] 房间码: $roomCode")

                transferJob = CoroutineScope(Dispatchers.Default).launch {
                    _state.value = TransferState.Connecting

                    val callback = object : ProgressCallback {
                        override fun onProgress(done: Long, total: Long, message: String): Boolean {
                            if (total > 0) {
                                _state.value = TransferState.Transferring(done, total, message)
                            } else if (message.isNotEmpty()) {
                                appendLog(message)
                            }
                            return !canceled
                        }
                    }

                    val result = NativeBridge.relayRecvFile(
                        relayHost, relayPort, roomCode, saveDir, callback
                    )

                    _state.value = when {
                        canceled -> { appendLog("[取消] 传输已取消"); TransferState.Canceled }
                        result == 0 -> { appendLog("[完成] 文件接收成功, 保存到: $saveDir"); TransferState.Done }
                        else -> {
                            appendLog("[失败] 错误码: $result (${NativeBridge.errorString(result)})")
                            TransferState.Error(result, NativeBridge.errorString(result))
                        }
                    }
                    autoReset()
                }
                return true
            }

            else -> {
                appendLog("[错误] 未知的二维码类型: ${parts[1]}")
                return false
            }
        }
    }

    // 取消当前传输
    fun cancel() {
        canceled = true
        appendLog("[取消] 正在终止传输...")
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
