// jni_bridge.cpp - JNI 接口层: 将 C++ 核心库导出给 Kotlin/Native 调用
// 对应 Kotlin 侧: com.filetransfer.protocol.NativeBridge
#include <jni.h>
#include <string>
#include <atomic>
#include <functional>
#include <memory>

#include "file_transfer.h"
#include "relay.h"
#include "socket_util.h"

// ===== 全局回调管理 =====
// JNI 回调需要跨线程 (C++ 传输线程 → JVM 回调线程)
struct JniCallbackContext {
    JavaVM* jvm;
    jobject callback_ref;           // GlobalRef of ProgressCallbackKt
    std::atomic<bool> canceled{false};
};

// 进度回调适配: C++ ProgressCallback → JNI 调用 Kotlin
static bool jni_progress_callback(uint64_t done, uint64_t total,
                                   const std::string& msg, JniCallbackContext* ctx) {
    if (ctx->canceled.load()) return false;

    JNIEnv* env = nullptr;
    bool attached = false;
    jint result = ctx->jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        ctx->jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (env == nullptr) return false;

    // 调用 Kotlin: ProgressCallback.onProgress(done: Long, total: Long, message: String): Boolean
    jclass cls = env->GetObjectClass(ctx->callback_ref);
    jmethodID method = env->GetMethodID(cls, "onProgress",
        "(JJLjava/lang/String;)Z");
    jstring jmsg = env->NewStringUTF(msg.c_str());
    jboolean should_continue = env->CallBooleanMethod(
        ctx->callback_ref, method,
        static_cast<jlong>(done),
        static_cast<jlong>(total),
        jmsg
    );
    env->DeleteLocalRef(jmsg);
    env->DeleteLocalRef(cls);

    if (attached) {
        ctx->jvm->DetachCurrentThread();
    }

    // 返回 false 表示 Kotlin 侧请求取消
    return should_continue == JNI_TRUE;
}

// ===== JNI 导出函数 =====
// 命名规则: Java_包名_类名_方法名 (对应 com.filetransfer.protocol.NativeBridge)
// 方法名必须与 Kotlin external 声明的方法名完全一致

extern "C" {

// 初始化网络 (Windows 需要 WSAStartup, Android/iOS 无需操作)
// Kotlin: external fun initNetworkNative(): Boolean
JNIEXPORT jboolean JNICALL
Java_com_filetransfer_protocol_NativeBridge_initNetworkNative(JNIEnv* env, jobject thiz) {
    return ft::init_network() ? JNI_TRUE : JNI_FALSE;
}

// 清理网络资源
// Kotlin: external fun cleanupNetworkNative()
JNIEXPORT void JNICALL
Java_com_filetransfer_protocol_NativeBridge_cleanupNetworkNative(JNIEnv* env, jobject thiz) {
    ft::cleanup_network();
}

// 错误码转文本
// Kotlin: external fun errorStringNative(code: Int): String
JNIEXPORT jstring JNICALL
Java_com_filetransfer_protocol_NativeBridge_errorStringNative(JNIEnv* env, jobject thiz, jint code) {
    std::string msg = ft::error_string(code);
    return env->NewStringUTF(msg.c_str());
}

// 中继发送文件
// Kotlin: external fun relaySendFileNative(host: String, port: Int, filePath: String, callback: ProgressCallback): Int
JNIEXPORT jint JNICALL
Java_com_filetransfer_protocol_NativeBridge_relaySendFileNative(
    JNIEnv* env, jobject thiz,
    jstring host, jint port, jstring file_path,
    jobject callback)
{
    const char* host_c = env->GetStringUTFChars(host, nullptr);
    const char* path_c = env->GetStringUTFChars(file_path, nullptr);

    // 获取 JVM 实例
    JavaVM* jvm;
    env->GetJavaVM(&jvm);
    jobject cb_ref = env->NewGlobalRef(callback);

    auto ctx = std::make_unique<JniCallbackContext>();
    ctx->jvm = jvm;
    ctx->callback_ref = cb_ref;

    // 构建 C++ ProgressCallback
    ft::ProgressCallback cb = [&ctx](uint64_t done, uint64_t total,
                                     const std::string& msg) -> bool {
        return jni_progress_callback(done, total, msg, ctx.get());
    };

    // 调用核心库
    int result = ft::relay_send_file(
        host_c,
        static_cast<unsigned short>(port),
        path_c,
        cb
    );

    // 清理
    env->ReleaseStringUTFChars(host, host_c);
    env->ReleaseStringUTFChars(file_path, path_c);
    env->DeleteGlobalRef(cb_ref);

    return static_cast<jint>(result);
}

// 中继接收文件
// Kotlin: external fun relayRecvFileNative(host: String, port: Int, roomCode: String, saveDir: String, callback: ProgressCallback): Int
JNIEXPORT jint JNICALL
Java_com_filetransfer_protocol_NativeBridge_relayRecvFileNative(
    JNIEnv* env, jobject thiz,
    jstring host, jint port, jstring room_code,
    jstring save_dir, jobject callback)
{
    const char* host_c = env->GetStringUTFChars(host, nullptr);
    const char* code_c = env->GetStringUTFChars(room_code, nullptr);
    const char* dir_c = env->GetStringUTFChars(save_dir, nullptr);

    JavaVM* jvm;
    env->GetJavaVM(&jvm);
    jobject cb_ref = env->NewGlobalRef(callback);

    auto ctx = std::make_unique<JniCallbackContext>();
    ctx->jvm = jvm;
    ctx->callback_ref = cb_ref;

    ft::ProgressCallback cb = [&ctx](uint64_t done, uint64_t total,
                                     const std::string& msg) -> bool {
        return jni_progress_callback(done, total, msg, ctx.get());
    };

    int result = ft::relay_recv_file(
        host_c,
        static_cast<unsigned short>(port),
        code_c,
        dir_c,
        cb
    );

    env->ReleaseStringUTFChars(host, host_c);
    env->ReleaseStringUTFChars(room_code, code_c);
    env->ReleaseStringUTFChars(save_dir, dir_c);
    env->DeleteGlobalRef(cb_ref);

    return static_cast<jint>(result);
}

// ===== iOS Swift Bridge 导出 (C 接口, 供 Swift 调用) =====
// iOS 不使用 JNI, 而是直接调用 C 函数 (通过 modulemap 暴露给 Swift)
#if !defined(__ANDROID__)
// 供 iOS 使用的 C 回调函数指针类型
using IosProgressCallback = bool(*)(uint64_t done, uint64_t total, const char* msg);

static IosProgressCallback g_ios_callback = nullptr;

void ft_set_ios_callback(IosProgressCallback cb) {
    g_ios_callback = cb;
}

int ft_relay_send_file_ios(const char* host, int port, const char* filepath) {
    ft::init_network();
    ft::ProgressCallback cb = [](uint64_t done, uint64_t total,
                                  const std::string& msg) -> bool {
        if (g_ios_callback) {
            return g_ios_callback(done, total, msg.c_str());
        }
        return true;
    };
    return ft::relay_send_file(host, static_cast<unsigned short>(port), filepath, cb);
}

int ft_relay_recv_file_ios(const char* host, int port,
                            const char* room_code, const char* save_dir) {
    ft::init_network();
    ft::ProgressCallback cb = [](uint64_t done, uint64_t total,
                                  const std::string& msg) -> bool {
        if (g_ios_callback) {
            return g_ios_callback(done, total, msg.c_str());
        }
        return true;
    };
    return ft::relay_recv_file(host, static_cast<unsigned short>(port),
                               room_code, save_dir, cb);
}
#endif

}  // extern "C"
