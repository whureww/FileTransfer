package com.filetransfer.ui

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Rect
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.journeyapps.barcodescanner.BarcodeView
import com.journeyapps.barcodescanner.BarcodeCallback
import com.journeyapps.barcodescanner.BarcodeResult
import kotlinx.coroutines.delay

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun QrScannerScreen(
    onQrCodeDetected: (String) -> Unit,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var hasScanned by remember { mutableStateOf(false) }
    var scanLineOffset by remember { mutableStateOf(0f) }
    val lifecycleOwner = LocalLifecycleOwner.current
    val barcodeViewRef = remember { mutableStateOf<BarcodeView?>(null) }

    val cameraPermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (!granted) onBack()
    }

    LaunchedEffect(Unit) {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) {
            cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    LaunchedEffect(Unit) {
        while (true) {
            scanLineOffset = 0f
            for (i in 0..100) {
                scanLineOffset = i / 100f
                delay(16)
            }
        }
    }

    // Lifecycle management for BarcodeView
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> barcodeViewRef.value?.resume()
                Lifecycle.Event.ON_PAUSE -> barcodeViewRef.value?.pause()
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            barcodeViewRef.value?.pause()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("扫描二维码") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "返回")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = Color.Black,
                    titleContentColor = Color.White,
                    navigationIconContentColor = Color.White
                )
            )
        },
        containerColor = Color.Black
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            AndroidView(
                factory = { ctx ->
                    BarcodeView(ctx).apply {
                        decodeContinuous(object : BarcodeCallback {
                            override fun barcodeResult(result: BarcodeResult) {
                                if (!hasScanned) {
                                    hasScanned = true
                                    result.text?.let { qr ->
                                        if (qr.isNotBlank()) {
                                            onQrCodeDetected(qr)
                                        }
                                    }
                                }
                            }
                            override fun possibleResultPoints(
                                points: List<com.google.zxing.ResultPoint>
                            ) {}
                        })
                    }
                },
                update = { barcodeView ->
                    barcodeViewRef.value = barcodeView
                    barcodeView.post {
                        val w = barcodeView.width
                        val h = barcodeView.height
                        if (w > 0 && h > 0) {
                            val frameSize = (minOf(w, h) * 0.75).toInt()
                            val left = (w - frameSize) / 2
                            val top = (h - frameSize) / 2
                            val rect = Rect(left, top, left + frameSize, top + frameSize)
                            barcodeView.setFramingRect(rect)
                        }
                    }
                },
                modifier = Modifier.fillMaxSize()
            )

            ScannerOverlay(scanLineOffset)

            Text(
                text = "将手机摄像头对准 PC 端生成的二维码",
                color = Color.White.copy(alpha = 0.85f),
                fontSize = 14.sp,
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .padding(bottom = 60.dp)
            )
        }
    }
}

@Composable
private fun ScannerOverlay(scanProgress: Float) {
    Box(modifier = Modifier.fillMaxSize()) {
        val frameColor = Color(0xFF00E676)

        Canvas(modifier = Modifier.fillMaxSize()) {
            val cw = size.width
            val ch = size.height
            val frameSize = minOf(cw, ch) * 0.75f
            val left = (cw - frameSize) / 2
            val top = (ch - frameSize) / 2
            val right = left + frameSize
            val bottom = top + frameSize

            // Dark overlay outside frame
            drawRect(Color.Black.copy(alpha = 0.55f),
                topLeft = Offset.Zero, size = Size(cw, top))
            drawRect(Color.Black.copy(alpha = 0.55f),
                topLeft = Offset(0f, bottom), size = Size(cw, ch - bottom))
            drawRect(Color.Black.copy(alpha = 0.55f),
                topLeft = Offset(0f, top), size = Size(left, frameSize))
            drawRect(Color.Black.copy(alpha = 0.55f),
                topLeft = Offset(right, top), size = Size(cw - right, frameSize))

            // Frame border
            val sw = 3f
            drawLine(frameColor, Offset(left, top), Offset(right, top), strokeWidth = sw)
            drawLine(frameColor, Offset(left, bottom), Offset(right, bottom), strokeWidth = sw)
            drawLine(frameColor, Offset(left, top), Offset(left, bottom), strokeWidth = sw)
            drawLine(frameColor, Offset(right, top), Offset(right, bottom), strokeWidth = sw)

            // Corner markers
            val cl = frameSize * 0.15f
            val cs = 6f
            drawLine(frameColor, Offset(left, top), Offset(left + cl, top), strokeWidth = cs)
            drawLine(frameColor, Offset(left, top), Offset(left, top + cl), strokeWidth = cs)
            drawLine(frameColor, Offset(right, top), Offset(right - cl, top), strokeWidth = cs)
            drawLine(frameColor, Offset(right, top), Offset(right, top + cl), strokeWidth = cs)
            drawLine(frameColor, Offset(left, bottom), Offset(left + cl, bottom), strokeWidth = cs)
            drawLine(frameColor, Offset(left, bottom), Offset(left, bottom - cl), strokeWidth = cs)
            drawLine(frameColor, Offset(right, bottom), Offset(right - cl, bottom), strokeWidth = cs)
            drawLine(frameColor, Offset(right, bottom), Offset(right, bottom - cl), strokeWidth = cs)

            // Scanning line
            val sy = top + (bottom - top) * scanProgress
            drawLine(
                color = frameColor,
                start = Offset(left + 10f, sy),
                end = Offset(right - 10f, sy),
                strokeWidth = 3f,
                pathEffect = androidx.compose.ui.graphics.PathEffect
                    .dashPathEffect(floatArrayOf(12f, 8f))
            )
        }
    }
}
