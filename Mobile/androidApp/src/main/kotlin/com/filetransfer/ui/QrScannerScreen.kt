package com.filetransfer.ui

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
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
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer
import com.google.zxing.qrcode.QRCodeReader
import kotlinx.coroutines.delay
import java.nio.ByteBuffer
import java.util.concurrent.Executors

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun QrScannerScreen(
    onQrCodeDetected: (String) -> Unit,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var scanLineOffset by remember { mutableStateOf(0f) }
    val lifecycleOwner = LocalLifecycleOwner.current
    var hasScanned by remember { mutableStateOf(false) }
    val executor = remember { Executors.newSingleThreadExecutor() }

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

    DisposableEffect(lifecycleOwner) {
        onDispose {
            executor.shutdown()
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
                }
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
                    PreviewView(ctx).apply {
                        scaleType = PreviewView.ScaleType.FILL_CENTER
                        implementationMode = PreviewView.ImplementationMode.COMPATIBLE
                        // Bind camera
                        val future = ProcessCameraProvider.getInstance(ctx)
                        future.addListener({
                            val provider = future.get()
                            val preview = Preview.Builder().build().also {
                                it.setSurfaceProvider(surfaceProvider)
                            }

                            val analysis = ImageAnalysis.Builder()
                                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                                .build()
                                .also { analysis ->
                                    analysis.setAnalyzer(executor, QrAnalyzer { result ->
                                        if (!hasScanned && result.isNotBlank()) {
                                            hasScanned = true
                                            onQrCodeDetected(result)
                                        }
                                    })
                                }

                            val selector = CameraSelector.DEFAULT_BACK_CAMERA

                            provider.unbindAll()
                            provider.bindToLifecycle(
                                lifecycleOwner,
                                selector,
                                preview,
                                analysis
                            )
                        }, ContextCompat.getMainExecutor(ctx))
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

/**
 * CameraX ImageAnalysis.Analyzer for QR code detection using zxing core
 */
private class QrAnalyzer(
    private val onQrDetected: (String) -> Unit
) : ImageAnalysis.Analyzer {

    private val reader = QRCodeReader()
    private val hints = mapOf(
        DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE),
        DecodeHintType.CHARACTER_SET to "UTF-8"
    )

    override fun analyze(image: ImageProxy) {
        try {
            val buffer: ByteBuffer = image.planes[0].buffer
            val data = ByteArray(buffer.remaining())
            buffer.get(data)

            val width = image.width
            val height = image.height

            // YUV to luminance source (zxing expects NV21/YUV format)
            val source = PlanarYUVLuminanceSource(
                data, width, height, 0, 0, width, height, false
            )

            val bitmap = BinaryBitmap(HybridBinarizer(source))
            val result = reader.decode(bitmap, hints)
            result?.text?.let { text ->
                if (text.isNotBlank()) {
                    onQrDetected(text)
                }
            }
        } catch (_: Exception) {
            // No QR code found in this frame, continue
        } finally {
            image.close()
        }
    }
}

@Composable
private fun ScannerOverlay(scanProgress: Float) {
    val frameColor = Color(0xFF00E676)

    Box(modifier = Modifier.fillMaxSize()) {
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
            drawLine(frameColor, Offset(left, top), Offset(right, top), strokeWidth = 3f)
            drawLine(frameColor, Offset(left, bottom), Offset(right, bottom), strokeWidth = 3f)
            drawLine(frameColor, Offset(left, top), Offset(left, bottom), strokeWidth = 3f)
            drawLine(frameColor, Offset(right, top), Offset(right, bottom), strokeWidth = 3f)

            // Corner markers
            val cl = frameSize * 0.15f
            drawLine(frameColor, Offset(left, top), Offset(left + cl, top), strokeWidth = 6f)
            drawLine(frameColor, Offset(left, top), Offset(left, top + cl), strokeWidth = 6f)
            drawLine(frameColor, Offset(right, top), Offset(right - cl, top), strokeWidth = 6f)
            drawLine(frameColor, Offset(right, top), Offset(right, top + cl), strokeWidth = 6f)
            drawLine(frameColor, Offset(left, bottom), Offset(left + cl, bottom), strokeWidth = 6f)
            drawLine(frameColor, Offset(left, bottom), Offset(left, bottom - cl), strokeWidth = 6f)
            drawLine(frameColor, Offset(right, bottom), Offset(right - cl, bottom), strokeWidth = 6f)
            drawLine(frameColor, Offset(right, bottom), Offset(right, bottom - cl), strokeWidth = 6f)

            // Scanning line
            val sy = top + (bottom - top) * scanProgress
            drawLine(
                color = frameColor,
                start = Offset(left + 10f, sy),
                end = Offset(right - 10f, sy),
                strokeWidth = 3f
            )
        }
    }
}
