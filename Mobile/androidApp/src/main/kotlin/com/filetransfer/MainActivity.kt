package com.filetransfer

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import com.filetransfer.ui.TransferScreen
import com.filetransfer.service.TransferService

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val service = TransferService()
        setContent {
            TransferScreen(service = service)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        // TransferService.destroy() 应在 ViewModel 中调用
    }
}
