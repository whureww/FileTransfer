package com.filetransfer

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.mutableStateOf
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.filetransfer.service.TransferService
import com.filetransfer.ui.QrScannerScreen
import com.filetransfer.ui.SplashScreen
import com.filetransfer.ui.TransferScreen
import com.filetransfer.ui.theme.FileTransferTheme
import java.io.File
import java.io.FileOutputStream

class MainActivity : ComponentActivity() {

    private lateinit var transferService: TransferService

    // 文件路径状态 (供 Compose UI 观察和更新)
    private val filePathState = mutableStateOf("")

    // 启动屏状态
    private val showSplashState = mutableStateOf(true)

    // 扫码界面状态
    private val showScannerState = mutableStateOf(false)

    // 默认保存目录 (app 专用 received 文件夹)
    private lateinit var defaultSaveDir: File

    private val pickFileLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri?.let {
            try {
                // 授予持久化读取权限
                contentResolver.takePersistableUriPermission(
                    it, Intent.FLAG_GRANT_READ_URI_PERMISSION
                )
            } catch (_: Exception) {}
            
            val path = copyUriToTempFile(it)
            if (path != null) {
                filePathState.value = path
            } else {
                Toast.makeText(this, "无法获取文件路径", Toast.LENGTH_SHORT).show()
            }
        }
    }

    // 目录选择器 (OpenDocumentTree 支持自定义保存位置)
    private val pickDirLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        uri?.let {
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            contentResolver.takePersistableUriPermission(it, flags)
            transferService.setCustomSaveDir(it.toString())
            Toast.makeText(this, "已设置自定义保存目录", Toast.LENGTH_SHORT).show()
        }
    }

    // ZXing 二维码扫描器 - 已替换为内嵌 BarcodeView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 初始化默认保存目录:
        // 优先使用公共目录 /storage/emulated/0/FileTransfer/received/ (用户可通过文件管理器直接访问)
        // 无权限时回退到 app 专用目录 /Android/data/com.filetransfer/files/received/
        val publicDir = File(Environment.getExternalStorageDirectory(), "FileTransfer/received")
        defaultSaveDir = if (publicDir.exists() || publicDir.mkdirs()) {
            publicDir
        } else {
            // 公共目录创建失败 (无 MANAGE_EXTERNAL_STORAGE 权限), 回退到 app 专用目录
            File(getExternalFilesDir(null), "received").apply { if (!exists()) mkdirs() }
        }

        transferService = TransferService().apply {
            saveDir = defaultSaveDir.absolutePath
        }

        // 请求文件访问权限 (Android 11+ 需要 MANAGE_EXTERNAL_STORAGE, 用于公共目录写入)
        requestStoragePermission()

        // 权限授予后, 如果之前回退到了 app 专用目录, 尝试切换到公共目录
        if (defaultSaveDir != publicDir && Environment.isExternalStorageManager()) {
            if (publicDir.exists() || publicDir.mkdirs()) {
                defaultSaveDir = publicDir
                transferService.saveDir = publicDir.absolutePath
            }
        }

        setContent {
            FileTransferTheme {
                when {
                    showSplashState.value -> {
                        SplashScreen(onFinished = { showSplashState.value = false })
                    }
                    showScannerState.value -> {
                        QrScannerScreen(
                            onQrCodeDetected = { qrContent ->
                                showScannerState.value = false
                                val saveDir = transferService.saveDir
                                val success = transferService.recvFileByScan(qrContent, saveDir)
                                if (!success) {
                                    Toast.makeText(this@MainActivity,
                                        "二维码内容无效, 请扫描正确的二维码",
                                        Toast.LENGTH_SHORT).show()
                                }
                            },
                            onBack = { showScannerState.value = false }
                        )
                    }
                    else -> {
                        TransferScreen(
                            service = transferService,
                            filePath = filePathState.value,
                            onFilePathChange = { filePathState.value = it },
                            onPickFile = { pickFileLauncher.launch(arrayOf("*/*")) },
                            saveDirProvider = { transferService.saveDir },
                            onPickSaveDir = { pickDirLauncher.launch(null) },
                            onScan = { showScannerState.value = true }
                        )
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        transferService.destroy()
    }

    /**
     * 请求文件访问权限:
     * - Android 11+ (API 30+): 请求 MANAGE_EXTERNAL_STORAGE
     * - Android 10 以下: 请求 READ/WRITE_EXTERNAL_STORAGE
     */
    private fun requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
                        data = Uri.parse("package:$packageName")
                    }
                    startActivity(intent)
                } catch (e: Exception) {
                    // 某些机型不支持此 Action, 回退到通用设置
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)
                }
            }
        } else {
            val permissions = arrayOf(
                android.Manifest.permission.READ_EXTERNAL_STORAGE,
                android.Manifest.permission.WRITE_EXTERNAL_STORAGE
            )
            ActivityCompat.requestPermissions(this, permissions, 1001)
        }
    }

    /**
     * 将 content:// URI 指向的文件复制到应用缓存目录, 返回实际文件路径
     * C++ 核心库需要实际文件路径 (ifstream), 无法直接处理 content:// URI
     * 兼容小米/MIUI 等非标准 URI 格式
     */
    private fun copyUriToTempFile(uri: Uri): String? {
        return try {
            val fileName = getFileNameFromUri(uri) ?: "transfer_file_${System.currentTimeMillis()}"
            val safeName = fileName.replace(Regex("[\\\\/:*?\"<>|]"), "_")
            // 加时间戳防重名
            val tempName = "ft_${System.currentTimeMillis()}_$safeName"

            val dir = getExternalFilesDir(null) ?: filesDir ?: cacheDir
            val tempFile = File(dir, tempName)

            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(tempFile).use { output ->
                    input.copyTo(output)
                }
            } ?: return null

            if (tempFile.exists() && tempFile.length() > 0) {
                tempFile.absolutePath
            } else {
                null
            }
        } catch (e: Exception) {
            null
        }
    }

    /**
     * 从 URI 获取文件名, 兼容多种 URI 格式:
     * - content://com.android.providers.media.documents/document/xxx
     * - content://com.android.providers.downloads.documents/document/xxx
     * - content://miui.miui.com/file/xxx (小米)
     * - file:///path/to/file
     */
    private fun getFileNameFromUri(uri: Uri): String? {
        // 方法1: 从 URI 路径末尾提取文件名
        val path = uri.path
        if (path != null) {
            val lastSeg = path.substringAfterLast('/')
            if (lastSeg.isNotEmpty() && lastSeg.contains(".")) {
                return lastSeg
            }
        }

        // 方法2: 通过 ContentResolver 查询 DISPLAY_NAME
        try {
            val proj = arrayOf(android.provider.OpenableColumns.DISPLAY_NAME)
            contentResolver.query(uri, proj, null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) {
                        val name = cursor.getString(idx)
                        if (name != null && name.isNotEmpty()) return name
                    }
                }
            }
        } catch (_: Exception) {}

        // 方法3: 从 URI 最后一个路径段获取
        try {
            val lastSeg = uri.lastPathSegment
            if (lastSeg != null && lastSeg.contains(".")) {
                return lastSeg.substringAfterLast(':')
            }
        } catch (_: Exception) {}

        return null
    }
}
