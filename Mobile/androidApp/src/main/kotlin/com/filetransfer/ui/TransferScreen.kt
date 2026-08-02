package com.filetransfer.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.filetransfer.model.TransferMode
import com.filetransfer.model.TransferState
import com.filetransfer.service.TransferService

@Composable
fun TransferScreen(service: TransferService) {
    val state by service.state.collectAsState()
    var mode by remember { mutableStateOf(TransferMode.SEND) }
    var filePath by remember { mutableStateOf("") }
    var roomCode by remember { mutableStateOf("") }
    var relayHost by remember { mutableStateOf(service.relayHost) }
    var relayPort by remember { mutableStateOf(service.relayPort.toString()) }

    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "FileTransfer v0.0.7",
            style = MaterialTheme.typography.headlineMedium,
            modifier = Modifier.padding(bottom = 24.dp)
        )

        // 模式切换
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            FilterChip(
                selected = mode == TransferMode.SEND,
                onClick = { mode = TransferMode.SEND },
                label = { Text("发送") },
                modifier = Modifier.weight(1f)
            )
            FilterChip(
                selected = mode == TransferMode.RECV,
                onClick = { mode = TransferMode.RECV },
                label = { Text("接收") },
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 中继服务器配置
        OutlinedTextField(
            value = relayHost,
            onValueChange = {
                relayHost = it
                service.relayHost = it
            },
            label = { Text("中继服务器地址") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )
        OutlinedTextField(
            value = relayPort,
            onValueChange = {
                relayPort = it
                it.toIntOrNull()?.let { p -> service.relayPort = p }
            },
            label = { Text("中继端口") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(16.dp))

        when (mode) {
            TransferMode.SEND -> SendPanel(
                state = state,
                filePath = filePath,
                onFilePathChange = { filePath = it },
                onStart = { service.sendFile(filePath) },
                onCancel = { service.cancel() },
                onReset = { service.reset() }
            )
            TransferMode.RECV -> RecvPanel(
                state = state,
                roomCode = roomCode,
                onRoomCodeChange = { roomCode = it },
                onStart = { service.recvFile(roomCode, "/storage/emulated/0/Download") },
                onCancel = { service.cancel() },
                onReset = { service.reset() }
            )
        }
    }
}

@Composable
private fun SendPanel(
    state: TransferState,
    filePath: String,
    onFilePathChange: (String) -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    OutlinedTextField(
        value = filePath,
        onValueChange = onFilePathChange,
        label = { Text("文件路径") },
        modifier = Modifier.fillMaxWidth()
    )
    Spacer(modifier = Modifier.height(8.dp))
    Button(onClick = onStart, modifier = Modifier.fillMaxWidth()) {
        Text("开始发送")
    }
    TransferStateView(state, onCancel, onReset)
}

@Composable
private fun RecvPanel(
    state: TransferState,
    roomCode: String,
    onRoomCodeChange: (String) -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    OutlinedTextField(
        value = roomCode,
        onValueChange = { v -> if (v.length <= 6) onRoomCodeChange(v.uppercase()) },
        label = { Text("输入 6 位房间码") },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii),
        modifier = Modifier.fillMaxWidth()
    )
    Spacer(modifier = Modifier.height(8.dp))
    Button(onClick = onStart, modifier = Modifier.fillMaxWidth()) {
        Text("开始接收")
    }
    TransferStateView(state, onCancel, onReset)
}

@Composable
private fun TransferStateView(
    state: TransferState,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    Spacer(modifier = Modifier.height(16.dp))
    when (state) {
        is TransferState.Idle -> {}

        is TransferState.Connecting -> {
            CircularProgressIndicator()
            Text("正在连接中继服务器...")
        }

        is TransferState.WaitingForPeer -> {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Text("房间码", fontSize = 14.sp)
                    Text(
                        state.roomCode,
                        fontSize = 48.sp,
                        letterSpacing = 8.sp
                    )
                    Text("将此房间码告知接收方")
                }
            }
        }

        is TransferState.Transferring -> {
            val progress = if (state.total > 0)
                state.done.toFloat() / state.total
            else 0f
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier.fillMaxWidth()
            )
            Text("${formatBytes(state.done)} / ${formatBytes(state.total)}")
            if (state.message.isNotEmpty()) Text(state.message)
            Button(onClick = onCancel) { Text("取消传输") }
        }

        is TransferState.Done -> {
            Text("传输完成", color = MaterialTheme.colorScheme.primary)
            Button(onClick = onReset) { Text("返回") }
        }

        is TransferState.Canceled -> {
            Text("已取消", color = MaterialTheme.colorScheme.tertiary)
            Button(onClick = onReset) { Text("返回") }
        }

        is TransferState.Error -> {
            Text("错误: ${state.message} (code=${state.code})",
                color = MaterialTheme.colorScheme.error)
            Button(onClick = onReset) { Text("返回") }
        }
    }
}

// 格式化字节数
private fun formatBytes(bytes: Long): String {
    if (bytes < 1024) return "$bytes B"
    val kb = bytes / 1024.0
    if (kb < 1024) return "%.1f KB".format(kb)
    val mb = kb / 1024.0
    if (mb < 1024) return "%.1f MB".format(mb)
    return "%.1f GB".format(mb / 1024.0)
}
