package com.filetransfer.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// 浅色主题 (主要使用)
private val LightColorScheme = lightColorScheme(
    primary = BrandPrimary,
    onPrimary = BrandOnPrimary,
    primaryContainer = BrandPrimaryContainer,
    onPrimaryContainer = BrandOnPrimaryContainer,
    secondary = BrandSecondary,
    secondaryContainer = BrandSecondaryContainer,
    onSecondary = Color.White,
    background = BrandBackground,
    onBackground = Color(0xFF1A1C1E),
    surface = BrandSurface,
    onSurface = Color(0xFF1A1C1E),
    surfaceVariant = BrandSurfaceVariant,
    onSurfaceVariant = BrandOnSurfaceVariant,
    error = BrandError,
    onError = Color.White,
    outline = BrandOutline,
)

// 深色主题
private val DarkColorScheme = darkColorScheme(
    primary = Color(0xFF7DCFE8),
    onPrimary = Color(0xFF003544),
    primaryContainer = Color(0xFF1F526B),
    onPrimaryContainer = Color(0xFFBEE9F5),
    secondary = Color(0xFFA4CDD9),
    onSecondary = Color(0xFF063540),
    background = Color(0xFF0F1112),
    onBackground = Color(0xFFE2E3E5),
    surface = Color(0xFF1A1C1E),
    onSurface = Color(0xFFE2E3E5),
    surfaceVariant = Color(0xFF2D3135),
    onSurfaceVariant = Color(0xFFBFC8CE),
    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    outline = Color(0xFF5A656C),
)

@Composable
fun FileTransferTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme
    MaterialTheme(
        colorScheme = colorScheme,
        typography = FileTransferTypography,
        content = content
    )
}
