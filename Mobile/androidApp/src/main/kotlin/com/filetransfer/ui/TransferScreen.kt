package com.filetransfer.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Send
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
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
    saveDirProvider: () -> String = { "/storage/emulated/0/Download" },
    onPickSaveDir: () -> Unit = {},
    onScan: () -> Unit = {}
) {
    val state by service.state.collectAsState()
    val logs by service.logs.collectAsState()
    val customSaveDir by service.customSaveDirUri.collectAsState()

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

    // 当前保存目录显示
    val currentSaveDir = customSaveDir?.let { "自定义目录" }
        ?: saveDirProvider().substringAfterLast("/")

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        BrandMark()
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "臻传 Silex",
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "v0.1.0",
                            fontSize = 12.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                },
                actions = {
                    if (connMode == ConnMode.RELAY) {
                        Box(
                            modifier = Modifier
                                .padding(end = 14.dp)
                                .size(34.dp)
                                .background(
                                    MaterialTheme.colorScheme.primaryContainer,
                                    CircleShape
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            IconButton(
                                onClick = { showAdvDialog = true },
                                enabled = !isBusy,
                                modifier = Modifier.size(34.dp)
                            ) {
                                Icon(
                                    Icons.Default.Settings,
                                    contentDescription = "高级设置",
                                    modifier = Modifier.size(18.dp),
                                    tint = MaterialTheme.colorScheme.onPrimaryContainer
                                )
                            }
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                    titleContentColor = MaterialTheme.colorScheme.onSurface
                )
            )
        },
        bottomBar = {
            NavigationBar(
                containerColor = MaterialTheme.colorScheme.surface,
                tonalElevation = 0.dp
            ) {
                BottomTab.entries.forEach { tab ->
                    NavigationBarItem(
                        selected = bottomTab == tab,
                        onClick = { if (!isBusy) bottomTab = tab },
                        icon = { Icon(tab.icon, contentDescription = null) },
                        label = { Text(tab.label) },
                        enabled = !isBusy,
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor = WarmDeep,
                            selectedTextColor = WarmDeep,
                            indicatorColor = WarmSoft,
                            unselectedIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                            unselectedTextColor = MaterialTheme.colorScheme.onSurfaceVariant
                        )
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
            SingleChoiceSegmentedButtonRow(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFFEBEEF7), RoundedCornerShape(12.dp))
            ) {
                val segColors = SegmentedButtonDefaults.colors(
                    activeContainerColor = MaterialTheme.colorScheme.surface,
                    activeContentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                    activeBorderColor = Color.Transparent,
                    inactiveContainerColor = Color.Transparent,
                    inactiveContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    inactiveBorderColor = Color(0xFFEBEEF7)
                )
                SegmentedButton(
                    selected = connMode == ConnMode.LAN,
                    onClick = { if (!isBusy) connMode = ConnMode.LAN },
                    shape = SegmentedButtonDefaults.itemShape(index = 0, count = 2),
                    colors = segColors
                ) {
                    Text("局域网直连")
                }
                SegmentedButton(
                    selected = connMode == ConnMode.RELAY,
                    onClick = { if (!isBusy) connMode = ConnMode.RELAY },
                    shape = SegmentedButtonDefaults.itemShape(index = 1, count = 2),
                    colors = segColors
                ) {
                    Text("房间码中继")
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // ===== 扫码接收按钮 (仅在房间码中继模式 + 接收标签显示) =====
            if (transferMode == TransferMode.RECV && connMode == ConnMode.RELAY) {
                OutlinedButton(
                    onClick = onScan,
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !isBusy,
                    shape = SectionCardShape,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary)
                ) {
                    Icon(Icons.Default.QrCodeScanner, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("扫码接收")
                }
                Spacer(modifier = Modifier.height(12.dp))
            }

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
                            onCancel = { service.cancel() }
                        )
                        TransferMode.RECV -> LanRecvPanel(
                            state = state,
                            lanPort = lanPort,
                            saveDir = currentSaveDir,
                            onPortChange = { lanPort = it },
                            onPickSaveDir = onPickSaveDir,
                            onStart = {
                                service.lanPort = lanPort.toIntOrNull() ?: 9090
                                service.recvFileLan(saveDirProvider())
                            },
                            onCancel = { service.cancel() }
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
                            onCancel = { service.cancel() }
                        )
                        TransferMode.RECV -> RelayRecvPanel(
                            state = state,
                            roomCode = roomCode,
                            saveDir = currentSaveDir,
                            onRoomCodeChange = { roomCode = it },
                            onPickSaveDir = onPickSaveDir,
                            onStart = { service.recvFileRelay(roomCode, saveDirProvider()) },
                            onCancel = { service.cancel() }
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

    // ===== 传输进度对话框 (扫码或手动触发的传输中均显示) =====
    if (state is TransferState.Connecting || state is TransferState.Transferring) {
        TransferProgressDialog(
            state = state,
            onCancel = { service.cancel() }
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
    onCancel: () -> Unit
) {
    SectionCard {
        Column {
            Text("局域网发送 (自动发现接收端)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            BrandOutlinedTextField(
                value = lanPort,
                onValueChange = onPortChange,
                label = { Text("端口") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
            )

            Spacer(Modifier.height(10.dp))

            WarmButton(
                text = "选择文件",
                onClick = onPickFile,
                icon = Icons.Default.AttachFile
            )

            if (filePath.isNotEmpty()) {
                Text(
                    text = "已选择: ${filePath.substringAfterLast('/')}",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 6.dp)
                )
            }

            Spacer(Modifier.height(8.dp))

            // 开始按钮在传输中变为取消按钮
            if (state is TransferState.Connecting || state is TransferState.Transferring) {
                CancelButton(onCancel)
            } else {
                GradientButton(
                    text = "发送",
                    onClick = onStart,
                    enabled = filePath.isNotEmpty()
                )
            }

            TransferStateView(state)
        }
    }
}

// ===== 局域网接收面板 =====
@Composable
private fun LanRecvPanel(
    state: TransferState,
    lanPort: String,
    saveDir: String,
    onPortChange: (String) -> Unit,
    onPickSaveDir: () -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit
) {
    SectionCard {
        Column {
            Text("局域网接收 (等待发送方连接)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            BrandOutlinedTextField(
                value = lanPort,
                onValueChange = onPortChange,
                label = { Text("端口") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
            )

            Spacer(Modifier.height(10.dp))

            // 保存目录显示 + 选择按钮
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "保存到: $saveDir",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.weight(1f)
                )
                TextButton(onClick = onPickSaveDir) {
                    Icon(Icons.Default.Folder, contentDescription = null, tint = WarmDeep)
                    Spacer(Modifier.width(4.dp))
                    Text("更改", color = WarmDeep)
                }
            }

            Spacer(Modifier.height(8.dp))

            if (state is TransferState.Connecting || state is TransferState.Transferring) {
                CancelButton(onCancel)
            } else {
                GradientButton(text = "开始接收", onClick = onStart)
            }

            TransferStateView(state)
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
    onCancel: () -> Unit
) {
    SectionCard {
        Column {
            Text("中继发送 (创建房间)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            WarmButton(
                text = "选择文件",
                onClick = onPickFile,
                icon = Icons.Default.AttachFile
            )

            if (filePath.isNotEmpty()) {
                Text(
                    text = "已选择: ${filePath.substringAfterLast('/')}",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 6.dp)
                )
            }

            Spacer(Modifier.height(8.dp))

            // 开始按钮在传输中变为取消按钮 (含等待对方加入)
            if (state is TransferState.Connecting || state is TransferState.Transferring ||
                state is TransferState.WaitingForPeer) {
                CancelButton(onCancel)
            } else {
                GradientButton(
                    text = "创建房间并发送",
                    onClick = onStart,
                    enabled = filePath.isNotEmpty()
                )
            }

            TransferStateView(state)
        }
    }
}

// ===== 中继接收面板 =====
@Composable
private fun RelayRecvPanel(
    state: TransferState,
    roomCode: String,
    saveDir: String,
    onRoomCodeChange: (String) -> Unit,
    onPickSaveDir: () -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit
) {
    SectionCard {
        Column {
            Text("中继接收 (输入房间码)", style = MaterialTheme.typography.titleMedium)

            Spacer(Modifier.height(12.dp))

            BrandOutlinedTextField(
                value = roomCode,
                onValueChange = { v -> if (v.length <= 6) onRoomCodeChange(v.uppercase()) },
                label = { Text("输入 6 位房间码") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii),
                textStyle = androidx.compose.ui.text.TextStyle(
                    fontSize = 24.sp,
                    letterSpacing = 4.sp
                )
            )

            Spacer(Modifier.height(10.dp))

            // 保存目录显示 + 选择按钮
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "保存到: $saveDir",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.weight(1f)
                )
                TextButton(onClick = onPickSaveDir) {
                    Icon(Icons.Default.Folder, contentDescription = null, tint = WarmDeep)
                    Spacer(Modifier.width(4.dp))
                    Text("更改", color = WarmDeep)
                }
            }

            Spacer(Modifier.height(8.dp))

            if (state is TransferState.Connecting || state is TransferState.Transferring) {
                CancelButton(onCancel)
            } else {
                GradientButton(
                    text = "加入房间并接收",
                    onClick = onStart,
                    enabled = roomCode.length == 6
                )
            }

            TransferStateView(state)
        }
    }
}

// ===== 品牌渐变颜色 (靛蓝, 与 PC 端一致) =====
private val GradientStart = Color(0xFF6B75D4)
private val GradientEnd = Color(0xFF818CF8)
// ===== 暖杏 (次要操作: 选择文件 / 取消 / 底部标签选中态) =====
private val WarmDeep = Color(0xFFC97F4E)
private val WarmSoft = Color(0xFFFBF0E4)
private val SectionCardShape = RoundedCornerShape(20.dp)
private val FieldShape = RoundedCornerShape(14.dp)

// ===== 品牌 Logo (靛蓝渐变圆角方块 + 白叶) =====
@Composable
private fun BrandMark(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .size(26.dp)
            .background(
                Brush.linearGradient(listOf(GradientStart, GradientEnd)),
                RoundedCornerShape(8.dp)
            ),
        contentAlignment = Alignment.Center
    ) {
        Canvas(modifier = Modifier.size(14.dp)) {
            val w = this.size.width
            val h = this.size.height
            val leaf = Path().apply {
                moveTo(w * 0.52f, h * 0.96f)
                cubicTo(w * 0.10f, h * 0.70f, w * 0.06f, h * 0.28f, w * 0.92f, h * 0.04f)
                cubicTo(w * 0.78f, h * 0.32f, w * 0.78f, h * 0.62f, w * 0.52f, h * 0.96f)
                close()
            }
            drawPath(leaf, Color.White)
        }
    }
}

// ===== 暖杏软底按钮 (选择文件) =====
@Composable
private fun WarmButton(
    text: String,
    onClick: () -> Unit,
    icon: ImageVector,
    modifier: Modifier = Modifier
) {
    Button(
        onClick = onClick,
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(999.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = WarmSoft,
            contentColor = WarmDeep
        )
    ) {
        Icon(icon, contentDescription = null, tint = WarmDeep)
        Spacer(Modifier.width(8.dp))
        Text(text)
    }
}

// ===== 品牌输入框 (浅底 + 聚焦靛蓝边框) =====
@Composable
private fun BrandOutlinedTextField(
    value: String,
    onValueChange: (String) -> Unit,
    label: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    textStyle: androidx.compose.ui.text.TextStyle = androidx.compose.ui.text.TextStyle.Default,
    placeholder: @Composable (() -> Unit)? = null
) {
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        label = label,
        placeholder = placeholder,
        singleLine = true,
        keyboardOptions = keyboardOptions,
        textStyle = textStyle,
        shape = FieldShape,
        modifier = modifier.fillMaxWidth(),
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor = Color(0xFF7882FF),
            unfocusedBorderColor = MaterialTheme.colorScheme.outline,
            focusedContainerColor = Color(0xFFFAFBFE),
            unfocusedContainerColor = Color(0xFFFAFBFE),
            cursorColor = GradientStart
        )
    )
}

// ===== 渐变主按钮 (蓝紫) =====
@Composable
private fun GradientButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        shape = SectionCardShape,
        color = Color.Transparent,
        modifier = modifier
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .background(Brush.horizontalGradient(listOf(GradientStart, GradientEnd)))
                .alpha(if (enabled) 1f else 0.45f)
                .padding(vertical = 14.dp),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text,
                color = Color.White,
                fontSize = 16.sp,
                fontWeight = FontWeight.SemiBold
            )
        }
    }
}

// ===== 统一的卡片容器 (圆角 + 浅色边框) =====
@Composable
private fun SectionCard(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = SectionCardShape,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.surfaceVariant)
    ) {
        Column(modifier = Modifier.padding(16.dp), content = content)
    }
}

// ===== 取消按钮 (暖杏软底) =====
@Composable
private fun CancelButton(onCancel: () -> Unit) {
    Button(
        onClick = onCancel,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(999.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = WarmSoft,
            contentColor = WarmDeep
        )
    ) { Text("取消") }
}

// ===== 传输状态视图 (进度/完成/错误, 无返回按钮) =====
@Composable
private fun TransferStateView(state: TransferState) {
    Spacer(modifier = Modifier.height(12.dp))
    when (state) {
        is TransferState.Idle -> {}

        is TransferState.Connecting -> {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(12.dp))
                Text("正在连接...", style = MaterialTheme.typography.bodyMedium)
            }
        }

        is TransferState.WaitingForPeer -> {
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = SectionCardShape,
                colors = CardDefaults.cardColors(containerColor = Color.Transparent)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Brush.linearGradient(listOf(GradientStart, GradientEnd)))
                        .padding(20.dp)
                ) {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text("房间码", fontSize = 13.sp, color = Color.White.copy(alpha = 0.85f))
                        Spacer(Modifier.height(4.dp))
                        Text(
                            state.roomCode,
                            fontSize = 36.sp,
                            fontWeight = FontWeight.Bold,
                            letterSpacing = 6.sp,
                            color = Color.White
                        )
                        Spacer(Modifier.height(4.dp))
                        Text("将此房间码告知接收方", style = MaterialTheme.typography.bodySmall,
                             color = Color.White.copy(alpha = 0.85f))
                    }
                }
            }
        }

        is TransferState.Transferring -> {
            val progress = if (state.total > 0)
                state.done.toFloat() / state.total
            else 0f
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(10.dp),
                color = GradientStart,
                trackColor = Color(0xFFECEFF7),
                strokeCap = androidx.compose.ui.graphics.StrokeCap.Round
            )
            Spacer(Modifier.height(6.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    "${formatBytes(state.done)} / ${formatBytes(state.total)}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    "${(progress * 100).toInt()}%",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Medium
                )
            }
            if (state.message.isNotEmpty()) {
                Spacer(Modifier.height(2.dp))
                Text(state.message, style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }

        is TransferState.Done -> {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    Icons.Default.Check,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
                Spacer(Modifier.width(6.dp))
                Text("传输完成", color = MaterialTheme.colorScheme.primary,
                     fontWeight = FontWeight.Medium)
            }
        }

        is TransferState.Canceled -> {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    Icons.Default.Close,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.width(6.dp))
                Text("已取消", color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }

        is TransferState.Error -> {
            Row(verticalAlignment = Alignment.Top) {
                Icon(
                    Icons.Default.Close,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp),
                    tint = MaterialTheme.colorScheme.error
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    "错误: ${state.message} (code=${state.code})",
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodySmall
                )
            }
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
            .heightIn(min = 120.dp, max = 280.dp),
        shape = SectionCardShape,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.surfaceVariant)
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
    var host by remember { mutableStateOf("") }
    var port by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("高级设置") },
        text = {
            Column {
                Text(
                    if (service.useCustomRelay) "当前: 已启用自定义服务器 (不显示地址)"
                    else "当前: 使用内置服务器 (默认)",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(12.dp))
                BrandOutlinedTextField(
                    value = host,
                    onValueChange = { host = it },
                    label = { Text("中继服务器 IP") },
                    placeholder = { Text("输入 IP 地址") }
                )
                Spacer(Modifier.height(8.dp))
                BrandOutlinedTextField(
                    value = port,
                    onValueChange = { port = it.filter { c -> c.isDigit() } },
                    label = { Text("端口") },
                    placeholder = { Text("端口号") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
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

// ===== 传输进度对话框 =====
@Composable
private fun TransferProgressDialog(
    state: TransferState,
    onCancel: () -> Unit
) {
    AlertDialog(
        onDismissRequest = { /* 不可点击外部关闭, 只能点取消 */ },
        title = {
            Text(
                when (state) {
                    is TransferState.Connecting -> "正在连接..."
                    is TransferState.Transferring -> "正在传输"
                    else -> "传输中"
                },
                fontWeight = FontWeight.SemiBold
            )
        },
        text = {
            Column(modifier = Modifier.fillMaxWidth()) {
                when (state) {
                    is TransferState.Connecting -> {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(20.dp),
                                strokeWidth = 2.dp
                            )
                            Spacer(Modifier.width(12.dp))
                            Text(
                                "正在连接, 请稍候...",
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                    is TransferState.Transferring -> {
                        val progress = if (state.total > 0)
                            state.done.toFloat() / state.total
                        else 0f
                        LinearProgressIndicator(
                            progress = { progress },
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(10.dp),
                            color = GradientStart,
                            trackColor = Color(0xFFECEFF7),
                            strokeCap = androidx.compose.ui.graphics.StrokeCap.Round
                        )
                        Spacer(Modifier.height(8.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                "${formatBytes(state.done)} / ${formatBytes(state.total)}",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                "${(progress * 100).toInt()}%",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.primary,
                                fontWeight = FontWeight.Medium
                            )
                        }
                        if (state.message.isNotEmpty()) {
                            Spacer(Modifier.height(4.dp))
                            Text(
                                state.message,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                    else -> {}
                }
            }
        },
        confirmButton = {
            Button(
                onClick = onCancel,
                shape = SectionCardShape,
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.error
                )
            ) { Text("取消传输") }
        }
    )
}
