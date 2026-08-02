package com.filetransfer.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Send
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.filetransfer.model.ConnMode
import com.filetransfer.model.TransferMode
import com.filetransfer.model.TransferState
import com.filetransfer.service.TransferService

// 底部导航栏项
private enum class BottomTab(val label: String, val icon: ImageVector) {
    SEND("发送文件", Icons.Default.Send),
    RECV("接收文件", Icons.Default.Download)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TransferScreen(
    service: TransferService,
    filePath: String,
    onFilePathChange: (String) -> Unit,
    onPickFile: () -> Unit = {},
    saveDirProvider: () -> String = { "/storage/emulated/0/Download" }
) {
    val state by service.state.collectAsState()
    val logs by service.logs.collectAsState()

    var connMode by rememberSaveable { mutableStateOf(ConnMode.LAN) }
    var bottomTab by rememberSaveable { mutableStateOf(BottomTab.SEND) }
    val transferMode = if (bottomTab == BottomTab.SEND) TransferMode.SEND else TransferMode.RECV

    // 中继模式: 房间码
    var roomCode by rememberSaveable { mutableStateOf("") }

    // 局域网端口
    var lanPort by rememberSaveable { mutableStateOf("9090") }

    // 高级设置对话框
    var showAdvDialog by remember { mutableStateOf(false) }

    val isBusy = state is TransferState.Connecting ||
        state is TransferState.Transferring ||
        state is TransferState.WaitingForPeer

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("FileTransfer v0.0.7") },
                actions = {
                    if (connMode == ConnMode.RELAY) {
                        IconButton(
                            onClick = { showAdvDialog = true },
                            enabled = !isBusy
                        ) {
                            Icon(Icons.Default.Settings, contentDescription = "高级设置")
                        }
                    }
                }
            )
        },
        bottomBar = {
            NavigationBar {
                BottomTab.entries.forEach { tab ->
                    NavigationBarItem(
                        selected = bottomTab == tab,
                        onClick = { if (!isBusy) bottomTab = tab },
                        icon = { Icon(tab.icon, contentDescription = null) },
                        label = { Text(tab.label) },
                        enabled = !isBusy
                    )
                }
            }
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
        ) {
            // ===== 顶部连接模式切换 (局域网 / 房间码中继) =====
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                SegmentedButton(
                    selected = connMode == ConnMode.LAN,
                    onClick = { if (!isBusy) connMode = ConnMode.LAN },
                    shape = SegmentedButtonDefaults.itemShape(index = 0, count = 2)
                ) {
                    Text("局域网直连")
                }
                SegmentedButton(
                    selected = connMode == ConnMode.RELAY,
                    onClick = { if (!isBusy) connMode = ConnMode.RELAY },
                    shape = SegmentedButtonDefaults.itemShape(index = 1, count = 2)
                ) {
                    Text("房间码中继")
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // ===== 模式内容区 =====
            when (connMode) {
                ConnMode.LAN -> {
                    when (transferMode) {
                        TransferMode.SEND -> LanSendPanel(
                            state = state,
                            filePath = filePath,
                            lanPort = lanPort,
                            onFilePathChange = onFilePathChange,
                            onPickFile = onPickFile,
                            onPortChange = { lanPort = it },
                            onStart = {
                                service.lanPort = lanPort.toIntOrNull() ?: 9090
                                service.sendFileLan(filePath)
                            },
                            onCancel = { service.cancel() },
                            onReset = { service.reset() }
                        )
                        TransferMode.RECV -> LanRecvPanel(
                            state = state,
                            lanPort = lanPort,
                            onPortChange = { lanPort = it },
                            onStart = {
                                service.lanPort = lanPort.toIntOrNull() ?: 9090
                                service.recvFileLan(saveDirProvider())
                            },
                            onCancel = { service.cancel() },
                            onReset = { service.reset() }
                        )
                    }
                }
                ConnMode.RELAY -> {
                    when (transferMode) {
                        TransferMode.SEND -> RelaySendPanel(
                            state = state,
                            filePath = filePath,
                            onFilePathChange = onFilePathChange,
                            onPickFile = onPickFile,
                            onStart = { service.sendFileRelay(filePath) },
                            onCancel = { service.cancel() },
                            onReset = { service.reset() }
                        )
                        TransferMode.RECV -> RelayRecvPanel(
                            state = state,
                            roomCode = roomCode,
                            onRoomCodeChange = { roomCode = it },
                            onStart = { service.recvFileRelay(roomCode, saveDirProvider()) },
                            onCancel = { service.cancel() },
                            onReset = { service.reset() }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // ===== 日志窗口 =====
            LogWindow(logs = logs)
        }
    }

    // ===== 高级设置对话框 =====
    if (showAdvDialog) {
        AdvancedRelayDialog(
            service = service,
            onDismiss = { showAdvDialog = false }
        )
    }
}

// ===== 局域网发送面板 =====
@Composable
private fun LanSendPanel(
    state: TransferState,
    filePath: String,
    lanPort: String,
    onFilePathChange: (String) -> Unit,
    onPickFile: () -> Unit,
    onPortChange: (String) -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("局域网发送 (自动发现接收端)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            OutlinedTextField(
                value = lanPort,
                onValueChange = onPortChange,
                label = { Text("端口") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(Modifier.height(8.dp))

            OutlinedButton(onClick = onPickFile, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.AttachFile, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text("选择文件")
            }

            if (filePath.isNotEmpty()) {
                Text(
                    text = "已选择: ${filePath.substringAfterLast('/')}",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 4.dp)
                )
            }

            Spacer(Modifier.height(8.dp))

            // 开始按钮在传输中变为取消按钮
            if (state is TransferState.Connecting || state is TransferState.Transferring) {
                Button(
                    onClick = onCancel,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) { Text("取消") }
            } else {
                Button(
                    onClick = onStart,
                    modifier = Modifier.fillMaxWidth(),
                    enabled = filePath.isNotEmpty()
                ) { Text("发送") }
            }

            TransferStateView(state, onCancel, onReset)
        }
    }
}

// ===== 局域网接收面板 =====
@Composable
private fun LanRecvPanel(
    state: TransferState,
    lanPort: String,
    onPortChange: (String) -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("局域网接收 (等待发送方连接)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            OutlinedTextField(
                value = lanPort,
                onValueChange = onPortChange,
                label = { Text("端口") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth()
            )

            Spacer(Modifier.height(8.dp))

            // 开始按钮在传输中变为取消按钮
            if (state is TransferState.Connecting || state is TransferState.Transferring) {
                Button(
                    onClick = onCancel,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) { Text("取消") }
            } else {
                Button(
                    onClick = onStart,
                    modifier = Modifier.fillMaxWidth()
                ) { Text("开始接收") }
            }

            TransferStateView(state, onCancel, onReset)
        }
    }
}

// ===== 中继发送面板 =====
@Composable
private fun RelaySendPanel(
    state: TransferState,
    filePath: String,
    onFilePathChange: (String) -> Unit,
    onPickFile: () -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("中继发送 (创建房间)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            OutlinedButton(onClick = onPickFile, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.AttachFile, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text("选择文件")
            }

            if (filePath.isNotEmpty()) {
                Text(
                    text = "已选择: ${filePath.substringAfterLast('/')}",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 4.dp)
                )
            }

            Spacer(Modifier.height(8.dp))

            // 开始按钮在传输中变为取消按钮 (含等待对方加入)
            if (state is TransferState.Connecting || state is TransferState.Transferring ||
                state is TransferState.WaitingForPeer) {
                Button(
                    onClick = onCancel,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) { Text("取消") }
            } else {
                Button(
                    onClick = onStart,
                    modifier = Modifier.fillMaxWidth(),
                    enabled = filePath.isNotEmpty()
                ) { Text("创建房间并发送") }
            }

            TransferStateView(state, onCancel, onReset)
        }
    }
}

// ===== 中继接收面板 =====
@Composable
private fun RelayRecvPanel(
    state: TransferState,
    roomCode: String,
    onRoomCodeChange: (String) -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("中继接收 (输入房间码)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            OutlinedTextField(
                value = roomCode,
                onValueChange = { v -> if (v.length <= 6) onRoomCodeChange(v.uppercase()) },
                label = { Text("输入 6 位房间码") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii),
                modifier = Modifier.fillMaxWidth(),
                textStyle = androidx.compose.ui.text.TextStyle(
                    fontSize = 24.sp,
                    letterSpacing = 4.sp
                )
            )

            Spacer(Modifier.height(8.dp))

            // 开始按钮在传输中变为取消按钮
            if (state is TransferState.Connecting || state is TransferState.Transferring) {
                Button(
                    onClick = onCancel,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) { Text("取消") }
            } else {
                Button(
                    onClick = onStart,
                    modifier = Modifier.fillMaxWidth(),
                    enabled = roomCode.length == 6
                ) { Text("加入房间并接收") }
            }

            TransferStateView(state, onCancel, onReset)
        }
    }
}

// ===== 传输状态视图 (进度/完成/错误) =====
@Composable
private fun TransferStateView(
    state: TransferState,
    onCancel: () -> Unit,
    onReset: () -> Unit
) {
    Spacer(modifier = Modifier.height(12.dp))
    when (state) {
        is TransferState.Idle -> {}

        is TransferState.Connecting -> {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(modifier = Modifier.size(24.dp))
                Spacer(Modifier.width(12.dp))
                Text("正在连接...")
            }
        }

        is TransferState.WaitingForPeer -> {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                )
            ) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Text("房间码", fontSize = 14.sp)
                    Text(
                        state.roomCode,
                        fontSize = 40.sp,
                        letterSpacing = 6.sp
                    )
                    Text("将此房间码告知接收方", style = MaterialTheme.typography.bodySmall)
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
            if (state.message.isNotEmpty()) {
                Text(state.message, style = MaterialTheme.typography.bodySmall)
            }
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
            Text(
                "错误: ${state.message} (code=${state.code})",
                color = MaterialTheme.colorScheme.error
            )
            Button(onClick = onReset) { Text("返回") }
        }
    }
}

// ===== 日志窗口 =====
@Composable
private fun LogWindow(logs: List<String>) {
    val scrollState = rememberScrollState()
    // 日志更新时自动滚动到底部
    LaunchedEffect(logs.size) {
        scrollState.animateScrollTo(scrollState.maxValue)
    }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 120.dp, max = 280.dp)
    ) {
        Column(modifier = Modifier.padding(8.dp)) {
            Text(
                "状态日志",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(Modifier.height(4.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(scrollState)
            ) {
                Text(
                    text = if (logs.isEmpty()) "(暂无日志)" else logs.joinToString("\n"),
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}

// ===== 高级设置对话框 (自定义中继服务器) =====
@Composable
private fun AdvancedRelayDialog(
    service: TransferService,
    onDismiss: () -> Unit
) {
    var host by remember {
        mutableStateOf(if (service.useCustomRelay) service.effectiveRelayHost() else "")
    }
    var port by remember {
        mutableStateOf(
            if (service.useCustomRelay) service.effectiveRelayPort().toString() else ""
        )
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("高级设置 - 自定义中继服务器") },
        text = {
            Column {
                OutlinedTextField(
                    value = host,
                    onValueChange = { host = it },
                    label = { Text("中继服务器 IP") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(
                    value = port,
                    onValueChange = { port = it.filter { c -> c.isDigit() } },
                    label = { Text("端口") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    "留空并确定可恢复使用默认中继服务器。",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val p = port.toIntOrNull()
                if (host.isBlank() || p == null || p <= 0) {
                    service.setCustomRelay(null, null)
                } else {
                    service.setCustomRelay(host.trim(), p)
                }
                onDismiss()
            }) { Text("确定") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("取消") }
        }
    )
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
