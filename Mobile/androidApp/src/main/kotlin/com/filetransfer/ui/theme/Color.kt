package com.filetransfer.ui.theme

import androidx.compose.ui.graphics.Color

// 方案 E「蓝暖杏」· 与 PC 端配色家族一致
// PC 端参考: 背景 RGB(245,247,252) · 主按钮渐变 RGB(107,117,212)->RGB(129,140,248) · 边框 RGB(224,230,240)
val BrandPrimary = Color(0xFF6B75D4)           // 主色 靛蓝 (PC 按钮渐变起点)
val BrandPrimaryDark = Color(0xFF5A63C4)       // 深靛
val BrandOnPrimary = Color(0xFFFFFFFF)
val BrandPrimaryContainer = Color(0xFFEEF0FB)  // 浅紫底 (图标/次要强调)
val BrandOnPrimaryContainer = Color(0xFF4A55B0)

val BrandGradientStart = Color(0xFF6B75D4)     // 渐变起点 (同 PC)
val BrandGradientEnd = Color(0xFF818CF8)       // 渐变终点 (同 PC)

val BrandSecondary = Color(0xFFE19A68)         // 辅色 暖杏 (选择文件/取消/选中态)
val BrandSecondaryDeep = Color(0xFFC97F4E)     // 暖杏深
val BrandSecondaryContainer = Color(0xFFFBF0E4) // 暖杏软底
val BrandOnSecondaryContainer = Color(0xFFC97F4E)

val BrandBackground = Color(0xFFF5F7FC)        // 底 浅蓝白 (同 PC)
val BrandSurface = Color(0xFFFFFFFF)
val BrandSurfaceVariant = Color(0xFFE4E9F2)    // 卡片边框/浅灰蓝 (同 PC)
val BrandOnSurface = Color(0xFF334155)         // 主文本
val BrandOnSurfaceVariant = Color(0xFF64748B)  // 次文本
val BrandOutline = Color(0xFFE4E9F2)

val BrandError = Color(0xFFE2574C)
val BrandGood = Color(0xFF22C55E)
