package com.filetransfer.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.filetransfer.ui.theme.BrandPrimary
import kotlinx.coroutines.delay

/**
 * 启动动画: 显示应用图标 + 名称, 淡出过渡到主界面
 * 动画时序: 0-300ms 图标缩放淡入, 300-1000ms 保持, 1000-1500ms 整体淡出
 */
@Composable
fun SplashScreen(onFinished: () -> Unit) {
    // 动画进度: 0 -> 1 (淡入), 保持, -> 0 (淡出)
    var phase by remember { mutableStateOf(0) }  // 0=淡入, 1=保持, 2=淡出

    // 淡入动画
    val enterScale by animateFloatAsState(
        targetValue = if (phase >= 1) 1f else 0.6f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessLow
        ),
        label = "splashScale"
    )
    val enterAlpha by animateFloatAsState(
        targetValue = if (phase >= 1) 1f else 0f,
        animationSpec = tween(durationMillis = 400, easing = FastOutSlowInEasing),
        label = "splashAlpha"
    )

    // 淡出动画
    val exitAlpha by animateFloatAsState(
        targetValue = if (phase >= 2) 0f else 1f,
        animationSpec = tween(durationMillis = 500, easing = FastOutSlowInEasing),
        label = "splashExit"
    )

    // 动画时序控制
    LaunchedEffect(Unit) {
        delay(100)
        phase = 1          // 触发淡入
        delay(800)         // 保持显示
        phase = 2          // 触发淡出
        delay(500)
        onFinished()       // 进入主界面
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .alpha(exitAlpha),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            // 应用图标 (使用自适应图标的前景矢量图)
            Box(
                modifier = Modifier
                    .size(96.dp)
                    .scale(enterScale)
                    .alpha(enterAlpha)
                    .background(
                        color = BrandPrimary,
                        shape = RoundedCornerShape(20.dp)
                    ),
                contentAlignment = Alignment.Center
            ) {
                // 简洁的文件传输图标: 文档 + 箭头
                SplashIcon(
                    modifier = Modifier.size(56.dp),
                    tint = Color.White
                )
            }

            Spacer(modifier = Modifier.height(20.dp))

            // 应用名称
            Text(
                text = "臻传 Silex",
                fontSize = 24.sp,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onBackground,
                modifier = Modifier
                    .alpha(enterAlpha)
                    .scale(enterScale)
            )

            Spacer(modifier = Modifier.height(6.dp))

            // 副标题
            Text(
                text = "局域网 / 中继 / 扫码传输",
                fontSize = 13.sp,
                fontWeight = FontWeight.Normal,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.alpha(enterAlpha)
            )

            Spacer(modifier = Modifier.height(32.dp))

            // 加载指示器 (细线条, 不突兀)
            CircularProgressIndicator(
                modifier = Modifier
                    .size(24.dp)
                    .alpha(enterAlpha),
                strokeWidth = 2.dp,
                color = BrandPrimary
            )
        }
    }
}

// 启动屏图标: 简洁的文件+箭头矢量图
@Composable
private fun SplashIcon(modifier: Modifier, tint: Color) {
    Image(
        imageVector = ImageVector.Builder(
            defaultWidth = 56.dp,
            defaultHeight = 56.dp,
            viewportWidth = 56f,
            viewportHeight = 56f
        ).apply {
            // 文档轮廓
            path(
                fill = SolidColor(Color.White),
                fillAlpha = 0.25f
            ) {
                moveTo(16f, 8f)
                lineTo(32f, 8f)
                lineTo(40f, 16f)
                lineTo(40f, 48f)
                lineTo(16f, 48f)
                close()
            }
            // 传输箭头
            path(fill = SolidColor(tint)) {
                moveTo(10f, 30f)
                lineTo(10f, 38f)
                lineTo(26f, 38f)
                lineTo(26f, 44f)
                lineTo(38f, 34f)
                lineTo(26f, 24f)
                lineTo(26f, 30f)
                close()
            }
        }.build(),
        contentDescription = null,
        modifier = modifier
    )
}
