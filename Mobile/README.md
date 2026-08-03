# Silex Mobile (KMP + C++ 核心库)

跨平台手机端文件传输 App，基于 **Kotlin Multiplatform (KMP)** + **C++ 核心库复用**，与桌面端协议完全一致。

## 架构

```
┌──────────────────────────────────────────┐
│         UI 层 (各平台原生)                 │
│  Android: Jetpack Compose                 │
│  iOS:     SwiftUI                          │
├──────────────────────────────────────────┤
│      KMP 共享逻辑层 (Kotlin)               │
│  TransferService / TransferState           │
│  NativeBridge (expect/actual)              │
├──────────────────────────────────────────┤
│      C++ 核心库 (从桌面端复用)              │
│  file_transfer / relay_client / socket_util│
│  jni_bridge.cpp (JNI 适配层)               │
├──────────────────────────────────────────┤
│      JNI / Swift Bridge                    │
│  Android: JNI (libfiletransfer_native.so)  │
│  iOS:     C 函数 → Swift Bridge            │
└──────────────────────────────────────────┘
```

## 项目结构

```
Mobile/
├── settings.gradle.kts          # KMP 项目配置
├── build.gradle.kts             # 根构建文件
├── gradle/
│   └── libs.versions.toml       # 版本目录
├── gradle.properties
│
├── shared/                      # KMP 共享模块
│   ├── build.gradle.kts         # KMP + Android NDK 配置
│   └── src/
│       ├── commonMain/kotlin/com/filetransfer/
│       │   ├── protocol/NativeBridge.kt      # expect 声明
│       │   ├── model/TransferState.kt        # 状态模型
│       │   └── service/TransferService.kt    # 共享业务逻辑
│       ├── androidMain/kotlin/.../
│       │   └── protocol/NativeBridge.android.kt  # JNI actual
│       └── iosMain/kotlin/.../
│           └── protocol/NativeBridge.ios.kt      # iOS actual
│
├── native/                      # C++ 核心库
│   ├── CMakeLists.txt           # CMake 构建 (Android NDK / iOS)
│   └── src/
│       ├── file_transfer.h/.cpp # 从桌面端复用 (协议核心)
│       ├── relay.h              # 中继协议头
│       ├── relay_client.cpp     # 中继客户端 (发送/接收)
│       ├── socket_util.h/.cpp   # 跨平台 socket 辅助
│       └── jni_bridge.cpp       # JNI 接口层 (新增)
│
├── androidApp/                  # Android 应用
│   ├── build.gradle.kts
│   └── src/main/
│       ├── AndroidManifest.xml
│       └── kotlin/com/filetransfer/
│           ├── MainActivity.kt
│           └── ui/TransferScreen.kt   # Compose UI
│
└── iosApp/                      # iOS 应用
    └── iosApp/
        ├── SilexApp.swift       # @main 入口
        ├── ContentView.swift           # SwiftUI UI
        ├── TransferViewModel.swift     # ViewModel
        └── Info.plist
```

## 与桌面端的协议兼容性

| 功能 | 桌面端 C++ | 手机端 C++ (复用) | 兼容 |
|------|-----------|-------------------|------|
| PacketHeader 布局 | 16字节 packed | 同 (零修改) | ✅ |
| 帧协议 (DATA/CANCEL/DONE) | 5字节帧头 | 同 (零修改) | ✅ |
| 中继协议 (CREATE/JOIN) | 文本行 | 同 (零修改) | ✅ |
| 安全校验 (帧大小/完整性/目录穿越) | v0.0.8 | 同 (零修改) | ✅ |
| 局域网 UDP 发现 | 支持 | 支持 (Android) | ✅ |
| 中继服务器 | 不修改 | 不修改 | ✅ |

## 构建指南

### 前置要求

- **JDK 17+**
- **Android Studio Hedgehog (2023.1) 或更高**
- **Android NDK 26.1.10909125+**
- **CMake 3.22.1+** (通过 Android Studio SDK Manager 安装)
- **Xcode 15+** (iOS 构建)
- **Kotlin Multiplatform Mobile 插件** (Android Studio 插件)

### Android 构建

```bash
cd D:\FileTransfer\Mobile

# 1. 配置 local.properties (指向 Android SDK)
echo "sdk.dir=C:\\Users\\<你的用户名>\\AppData\\Local\\Android\\Sdk" > local.properties

# 2. Debug 构建
./gradlew :androidApp:assembleDebug

# 3. 输出 APK
# androidApp/build/outputs/apk/debug/androidApp-debug.apk
```

或在 Android Studio 中直接打开 `Mobile/` 目录，等待 Gradle Sync 完成后点击 Run。

### iOS 构建

```bash
cd D:\FileTransfer\Mobile

# 1. 生成 Xcode 项目 (需 macOS)
./gradlew :shared:linkDebugFrameworkIosArm64

# 2. 用 Xcode 打开 iosApp
open iosApp/iosApp.xcodeproj

# 3. 在 Xcode 中:
#    - 添加 shared framework 到 Embedded Frameworks
#    - 配置 C++ 核心库 (通过 Podfile 或手动添加)
#    - 选择真机/模拟器后 Build & Run
```

### C++ 核心库单独编译 (调试用)

```bash
cd D:\FileTransfer\Mobile\native

# Android (arm64-v8a)
cmake -B build-android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
cmake --build build-android

# iOS (arm64)
cmake -B build-ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-ios
```

## 中继服务器

手机端使用与桌面端相同的中继服务器 (`Relay/SilexRelay.exe`)，无需修改。

默认中继地址在 `TransferService` 中配置:
```kotlin
var relayHost: String = "your-relay-server.com"
var relayPort: Int = 9091
```

UI 层可修改此地址。

## 技术栈版本

| 组件 | 版本 |
|------|------|
| Kotlin | 2.0.20 |
| Android Gradle Plugin | 8.5.2 |
| Compose Multiplatform | 1.6.11 |
| Kotlinx Coroutines | 1.8.1 |
| C++ Standard | C++17 |
| Android minSdk | 24 (Android 7.0) |
| Android targetSdk | 34 (Android 14) |
| iOS Deployment Target | 15.0+ |

## 已知限制

1. **iOS C++ 桥接未完成**: `NativeBridge.ios.kt` 中的 `relaySendFile`/`relayRecvFile` 返回 -1，需通过 Swift Bridge 完成实际调用
2. **文件选择器**: Android 端文件路径需手动输入，后续应集成 `ActivityResultContracts.GetContent`
3. **局域网直连**: Android 支持 UDP 广播发现，iOS 受系统限制可能无法使用
4. **后台传输**: 当前传输在前台进行，切后台可能被系统终止，后续需用 WorkManager
