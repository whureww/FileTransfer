package com.filetransfer.model

// 传输状态 (UI 层观察此状态)
sealed class TransferState {
    // 空闲
    object Idle : TransferState()

    // 正在连接 (局域网发现 / 中继服务器)
    object Connecting : TransferState()

    // 发送方: 房间码已生成, 等待接收方加入
    data class WaitingForPeer(val roomCode: String) : TransferState()

    // 正在传输文件
    data class Transferring(
        val done: Long,
        val total: Long,
        val message: String
    ) : TransferState()

    // 传输完成
    object Done : TransferState()

    // 已取消
    object Canceled : TransferState()

    // 出错
    data class Error(val code: Int, val message: String) : TransferState()
}

// 连接模式: 局域网直连 / 房间码中继
enum class ConnMode {
    LAN,    // 局域网直连 (默认)
    RELAY   // 房间码中继 (跨局域网)
}

// 操作方向: 发送 / 接收
enum class TransferMode {
    SEND,  // 发送文件
    RECV   // 接收文件
}
