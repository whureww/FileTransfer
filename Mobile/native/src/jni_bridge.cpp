// jni_bridge.cpp - JNI 接口层: 将 C++ 核心库导出给 Kotlin 调用
// 对应 Kotlin 侧: com.filetransfer.protocol.NativeBridge
#include <jni.h>
#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "file_transfer.h"
#include "relay.h"
#include "secret.h"
#include "socket_util.h"

// ===== 全局回调管理 =====
struct JniCallbackContext {
    JavaVM* jvm;
    jobject callback_ref;
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
    return should_continue == JNI_TRUE;
}

// 创建回调上下文 (供所有传输函数复用)
static std::unique_ptr<JniCallbackContext> make_callback_ctx(JNIEnv* env, jobject callback) {
    JavaVM* jvm;
    env->GetJavaVM(&jvm);
    jobject cb_ref = env->NewGlobalRef(callback);
    auto ctx = std::make_unique<JniCallbackContext>();
    ctx->jvm = jvm;
    ctx->callback_ref = cb_ref;
    return ctx;
}

static ft::ProgressCallback make_progress_cb(JniCallbackContext* ctx) {
    return [ctx](uint64_t done, uint64_t total, const std::string& msg) -> bool {
        return jni_progress_callback(done, total, msg, ctx);
    };
}

static void release_callback_ctx(JNIEnv* env, std::unique_ptr<JniCallbackContext>& ctx) {
    if (ctx && ctx->callback_ref) {
        env->DeleteGlobalRef(ctx->callback_ref);
        ctx->callback_ref = nullptr;
    }
}

// ===== JNI 导出函数 =====
extern "C" {

// ---------- 网络 ----------

JNIEXPORT jboolean JNICALL
Java_com_filetransfer_protocol_NativeBridge_initNetworkNative(JNIEnv* env, jobject thiz) {
    return ft::init_network() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_filetransfer_protocol_NativeBridge_cleanupNetworkNative(JNIEnv* env, jobject thiz) {
    ft::cleanup_network();
}

JNIEXPORT jstring JNICALL
Java_com_filetransfer_protocol_NativeBridge_errorStringNative(JNIEnv* env, jobject thiz, jint code) {
    std::string msg = ft::error_string(code);
    return env->NewStringUTF(msg.c_str());
}

// ---------- 中继服务器地址 ----------

// 返回默认中继服务器地址 "host:port" (从加密配置解密)
JNIEXPORT jstring JNICALL
Java_com_filetransfer_protocol_NativeBridge_getRelayAddrNative(JNIEnv* env, jobject thiz) {
    std::string host;
    unsigned short port = 0;
    if (ft::parse_relay_addr(host, port)) {
        std::string addr = host + ":" + std::to_string(port);
        return env->NewStringUTF(addr.c_str());
    }
    return env->NewStringUTF("");
}

// ---------- 局域网直连: 发送 ----------

// ip 为空时自动发现局域网接收端
JNIEXPORT jint JNICALL
Java_com_filetransfer_protocol_NativeBridge_sendFileNative(
    JNIEnv* env, jobject thiz,
    jstring ip, jint port, jstring file_path, jobject callback)
{
    const char* ip_c = (ip ? env->GetStringUTFChars(ip, nullptr) : "");
    const char* path_c = env->GetStringUTFChars(file_path, nullptr);

    auto ctx = make_callback_ctx(env, callback);
    ft::ProgressCallback cb = make_progress_cb(ctx.get());

    std::string ip_str(ip_c);

    // ip 为空 → 自动发现接收端
    if (ip_str.empty()) {
        cb(0, 0, "[信息] 正在搜索局域网内的接收端...");
        auto peers = ft::discover_peers(static_cast<unsigned short>(port), 1500);
        if (peers.empty()) {
            cb(0, 0, "[错误] 未发现局域网内的接收端 (请确认接收方已启动并使用相同端口)");
            env->ReleaseStringUTFChars(file_path, path_c);
            if (ip) env->ReleaseStringUTFChars(ip, ip_c);
            release_callback_ctx(env, ctx);
            return static_cast<jint>(ft::ERR_CONNECT);
        }
        if (peers.size() > 1) {
            std::string msg = "[信息] 发现 " + std::to_string(peers.size())
                + " 个接收端, 将连接第一个: " + peers[0].first;
            cb(0, 0, msg);
        } else {
            cb(0, 0, "[信息] 发现接收端: " + peers[0].first
                + ":" + std::to_string(peers[0].second));
        }
        ip_str = peers[0].first;
    }

    int result = ft::send_file(ip_str, static_cast<unsigned short>(port), path_c, cb);

    env->ReleaseStringUTFChars(file_path, path_c);
    if (ip) env->ReleaseStringUTFChars(ip, ip_c);
    release_callback_ctx(env, ctx);
    return static_cast<jint>(result);
}

// ---------- 局域网直连: 接收 ----------

// 内部启动 UDP 发现响应线程, 阻塞等待发送端连接
JNIEXPORT jint JNICALL
Java_com_filetransfer_protocol_NativeBridge_recvFileNative(
    JNIEnv* env, jobject thiz,
    jint port, jstring save_dir, jobject callback)
{
    const char* dir_c = env->GetStringUTFChars(save_dir, nullptr);

    auto ctx = make_callback_ctx(env, callback);
    ft::ProgressCallback cb = make_progress_cb(ctx.get());

    unsigned short tcp_port = static_cast<unsigned short>(port);

    // 启动 UDP 发现响应线程 (供发送端自动发现本机)
    std::atomic<bool> discovery_running{true};
    std::thread discovery_worker = ft::start_discovery_responder(tcp_port, discovery_running);

    cb(0, 0, "[信息] 已开启局域网自动发现, 等待发送端连接...");

    int result = ft::recv_file(tcp_port, dir_c, cb);

    // 停止发现响应线程
    discovery_running = false;
    if (discovery_worker.joinable()) discovery_worker.join();

    env->ReleaseStringUTFChars(save_dir, dir_c);
    release_callback_ctx(env, ctx);
    return static_cast<jint>(result);
}

// ---------- 客户端接收 (HTTP 直连模式: 连接到远端 IP:port 接收文件) ----------

JNIEXPORT jint JNICALL
Java_com_filetransfer_protocol_NativeBridge_connectRecvNative(
    JNIEnv* env, jobject thiz,
    jstring ip, jint port, jstring save_dir, jobject callback)
{
    const char* ip_c = env->GetStringUTFChars(ip, nullptr);
    const char* dir_c = env->GetStringUTFChars(save_dir, nullptr);

    auto ctx = make_callback_ctx(env, callback);
    ft::ProgressCallback cb = make_progress_cb(ctx.get());

    int result = ft::connect_recv(
        ip_c, static_cast<unsigned short>(port), dir_c, cb);

    env->ReleaseStringUTFChars(ip, ip_c);
    env->ReleaseStringUTFChars(save_dir, dir_c);
    release_callback_ctx(env, ctx);
    return static_cast<jint>(result);
}

// ---------- 中继: 发送 ----------

JNIEXPORT jint JNICALL
Java_com_filetransfer_protocol_NativeBridge_relaySendFileNative(
    JNIEnv* env, jobject thiz,
    jstring host, jint port, jstring file_path, jobject callback)
{
    const char* host_c = env->GetStringUTFChars(host, nullptr);
    const char* path_c = env->GetStringUTFChars(file_path, nullptr);

    auto ctx = make_callback_ctx(env, callback);
    ft::ProgressCallback cb = make_progress_cb(ctx.get());

    int result = ft::relay_send_file(
        host_c, static_cast<unsigned short>(port), path_c, cb);

    env->ReleaseStringUTFChars(host, host_c);
    env->ReleaseStringUTFChars(file_path, path_c);
    release_callback_ctx(env, ctx);
    return static_cast<jint>(result);
}

// ---------- 中继: 接收 ----------

JNIEXPORT jint JNICALL
Java_com_filetransfer_protocol_NativeBridge_relayRecvFileNative(
    JNIEnv* env, jobject thiz,
    jstring host, jint port, jstring room_code,
    jstring save_dir, jobject callback)
{
    const char* host_c = env->GetStringUTFChars(host, nullptr);
    const char* code_c = env->GetStringUTFChars(room_code, nullptr);
    const char* dir_c = env->GetStringUTFChars(save_dir, nullptr);

    auto ctx = make_callback_ctx(env, callback);
    ft::ProgressCallback cb = make_progress_cb(ctx.get());

    int result = ft::relay_recv_file(
        host_c, static_cast<unsigned short>(port), code_c, dir_c, cb);

    env->ReleaseStringUTFChars(host, host_c);
    env->ReleaseStringUTFChars(room_code, code_c);
    env->ReleaseStringUTFChars(save_dir, dir_c);
    release_callback_ctx(env, ctx);
    return static_cast<jint>(result);
}

// ---------- 获取本机 IP 列表 (接收端显示, 供发送方手动输入) ----------

JNIEXPORT jstring JNICALL
Java_com_filetransfer_protocol_NativeBridge_getLocalIpsNative(JNIEnv* env, jobject thiz) {
    auto ips = ft::get_local_ipv4_addresses();
    std::string result;
    for (size_t i = 0; i < ips.size(); ++i) {
        if (i > 0) result += "\n";
        result += ips[i];
    }
    return env->NewStringUTF(result.c_str());
}

}  // extern "C"
