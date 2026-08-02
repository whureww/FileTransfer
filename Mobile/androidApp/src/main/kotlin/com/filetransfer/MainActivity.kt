package com.filetransfer

import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import com.filetransfer.service.TransferService
import com.filetransfer.ui.TransferScreen
import java.io.File
import java.io.FileOutputStream

class MainActivity : ComponentActivity() {

    private lateinit var transferService: TransferService

    private val pickFileLauncher = registerForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri ->
        uri?.let {
            // 将 URI 转为实际文件路径 (通过 DocumentsContract 或直接复制到缓存)
            val filePath = copyUriToTempFile(it)
            if (filePath != null) {
                transferService.sendFile(filePath)
            } else {
                Toast.makeText(this, "无法获取文件路径", Toast.LENGTH_SHORT).show()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        transferService = TransferService()

        setContent {
            TransferScreen(
                service = transferService,
                onPickFile = { pickFileLauncher.launch("*/*") }
            )
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        transferService.destroy()
    }

    /**
     * 将 content:// URI 指向的文件复制到应用缓存目录, 返回实际文件路径
     * 这是因为 C++ 核心库需要实际文件路径 (ifstream), 无法直接处理 content:// URI
     */
    private fun copyUriToTempFile(uri: Uri): String? {
        return try {
            val fileName = getFileNameFromUri(uri) ?: "transfer_file"
            val safeName = fileName.replace(Regex("[\\\\/:*?\"<>|]"), "_")

            val dir = getExternalFilesDir(null) ?: filesDir ?: cacheDir
            val tempFile = File(dir, "ft_$safeName")

            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(tempFile).use { output ->
                    input.copyTo(output)
                }
            }

            tempFile.absolutePath
        } catch (e: Exception) {
            null
        }
    }

    private fun getFileNameFromUri(uri: Uri): String? {
        // 尝试 DocumentsContract
        DocumentsContract.getDocumentId(uri)?.let { documentId ->
            val split = documentId.split(":")
            if (split.size > 1) return split[1]
        }

        // 尝试 OpenableColumns
        return try {
            val proj = arrayOf(android.provider.OpenableColumns.DISPLAY_NAME)
            contentResolver.query(uri, proj, null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    cursor.getString(0)
                } else null
            }
        } catch (e: Exception) {
            null
        }
    }
}
