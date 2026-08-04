package com.filetransfer.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// 浅色主题 (主要使用, 方案 E 蓝暖杏)
private val LightColorScheme = lightColorScheme(
    primary = BrandPrimary,
    onPrimary = BrandOnPrimary,
    primaryContainer = BrandPrimaryContainer,
    onPrimaryContainer = BrandOnPrimaryContainer,
    secondary = BrandSecondary,
    onSecondary = BrandOnPrimary,
    secondaryContainer = BrandSecondaryContainer,
    onSecondaryContainer = BrandOnSecondaryContainer,
    background = BrandBackground,
    onBackground = BrandOnSurface,
    surface = BrandSurface,
    onSurface = BrandOnSurface,
    surfaceVariant = BrandSurfaceVariant,
    onSurfaceVariant = BrandOnSurfaceVariant,
    error = BrandError,
    onError = Color.White,
    outline = BrandOutline,
)

// 深色主题 (靛蓝系, 保持品牌一致)
private val DarkColorScheme = darkColorScheme(
    primary = Color(0xFF9DA5F2),
    onPrimary = Color(0xFF262F7A),
    primaryContainer = Color(0xFF3A4391),
    onPrimaryContainer = Color(0xFFDEE1FF),
    secondary = Color(0xFFF2B890),
    onSecondary = Color(0xFF5C3A1F),
    secondaryContainer = Color(0xFF7A5230),
    onSecondaryContainer = Color(0xFFFFDCC4),
    background = Color(0xFF14171F),
    onBackground = Color(0xFFE4E7EE),
    surface = Color(0xFF1B1F29),
    onSurface = Color(0xFFE4E7EE),
    surfaceVariant = Color(0xFF2C323E),
    onSurfaceVariant = Color(0xFFC2C8D4),
    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    outline = Color(0xFF5A616E),
)

@Composable
fun SilexTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme
    MaterialTheme(
        colorScheme = colorScheme,
        typography = SilexTypography,
        content = content
    )
}
