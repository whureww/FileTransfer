// socket_util.cpp - 跨平台 socket/IO 辅助函数实现
#include "socket_util.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace ft {

// 错误码转可读文本
std::string error_string(int code) {
    switch (code) {
        case OK:              return "成功";
        case ERR_OPEN_FILE:   return "无法打开文件";
        case ERR_FILE_SIZE:   return "无法获取文件大小";
        case ERR_SOCKET:      return "创建 socket 失败";
        case ERR_CONNECT:     return "连接失败";
        case ERR_BIND:        return "绑定端口失败";
        case ERR_LISTEN:      return "监听失败";
        case ERR_RECV_HDR:    return "接收头部失败";
        case ERR_BAD_MAGIC:   return "协议魔数/版本不匹配";
        case ERR_BAD_NAME:    return "文件名长度异常";
        case ERR_RECV_NAME:   return "接收文件名失败";
        case ERR_CREATE_FILE: return "创建输出文件失败";
        case ERR_SEND_HDR:    return "发送头部失败";
        case ERR_RECV_DATA:   return "接收数据失败";
        case ERR_WRITE_FILE:  return "写入文件失败";
        case ERR_SEND_DATA:   return "发送数据失败";
        case ERR_READ_FILE:   return "读取文件失败";
        case ERR_RELAY_LINE:  return "中继协议通信失败";
        case ERR_RELAY_CODE:  return "房间码格式错误";
        case ERR_RELAY_ROOM:  return "房间不存在或已满";
        case ERR_RELAY_PEER:  return "对端异常断开";
        case CANCELED:        return "用户取消";
        default:              return "未知错误 (code=" + std::to_string(code) + ")";
    }
}

namespace detail {

#ifdef _WIN32
std::wstring utf8_to_wpath(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}
static std::string wpath_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
    return s;
}
#endif

bool get_file_size(std::ifstream& in, uint64_t& size) {
    in.seekg(0, std::ios::end);
    std::streampos pos = in.tellg();
    if (pos < 0) return false;
    size = static_cast<uint64_t>(pos);
    in.seekg(0, std::ios::beg);
    return true;
}

std::string basename(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

std::string normalize_dir(const std::string& dir, const ProgressCallback& cb) {
    namespace fs = std::filesystem;
#ifdef _WIN32
    fs::path p = dir.empty() ? fs::path(L".") : fs::path(utf8_to_wpath(dir));
#else
    fs::path p = dir.empty() ? fs::path(".") : fs::path(dir);
#endif
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec && !fs::is_directory(p)) {
        if (cb) cb(0, 0, "[警告] 创建输出目录失败: " + ec.message());
    }
#ifdef _WIN32
    std::wstring ws = p.wstring();
    if (!ws.empty() && ws.back() != L'/' && ws.back() != L'\\') {
        ws += fs::path::preferred_separator;
    }
    return wpath_to_utf8(ws);
#else
    std::string s = p.string();
    if (!s.empty() && s.back() != '/' && s.back() != '\\') {
        s += fs::path::preferred_separator;
    }
    return s;
#endif
}

bool send_all(socket_t sock, const char* buf, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        int n = ::send(sock, buf + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_all(socket_t sock, char* buf, std::size_t len) {
    std::size_t received = 0;
    while (received < len) {
        int n = ::recv(sock, buf + received, static_cast<int>(len - received), 0);
        if (n <= 0) return false;
        received += static_cast<std::size_t>(n);
    }
    return true;
}

bool report(const ProgressCallback& cb, uint64_t done, uint64_t total,
            const std::string& msg) {
    if (cb) return cb(done, total, msg);
    if (total > 0) {
        double pct = total == 0 ? 100.0 : 100.0 * static_cast<double>(done) / static_cast<double>(total);
        const int bar_width = 40;
        int filled = static_cast<int>(bar_width * pct / 100.0);
        if (filled > bar_width) filled = bar_width;
        std::cout << "\r[";
        for (int i = 0; i < bar_width; ++i) std::cout << (i < filled ? '#' : '.');
        std::cout << "] " << pct << "%  (" << done << "/" << total << " bytes)";
        std::cout.flush();
        if (done >= total) std::cout << "\n";
    } else {
        std::cout << msg << "\n";
    }
    return true;
}

bool recv_line(socket_t sock, std::string& line, std::size_t maxLen) {
    line.clear();
    char c = 0;
    while (line.size() < maxLen) {
        int n = ::recv(sock, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') return true;
        if (c != '\r') line.push_back(c);
    }
    return false;  // 超长, 视为非法
}

bool send_line(socket_t sock, const std::string& s) {
    std::string buf = s + "\n";
    return send_all(sock, buf.data(), buf.size());
}

std::string sanitize_filename(const std::string& name) {
    std::string safe_name;
    safe_name.reserve(name.size());
    for (char c : name) {
        if (c == '/' || c == '\\') continue;          // 路径分隔符
        if (c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') continue;  // Windows 非法字符
        if (static_cast<unsigned char>(c) < 0x20) continue;  // 控制字符
        safe_name.push_back(c);
    }
    if (safe_name.empty()) safe_name = "received_file";
    // 防止 Windows 保留设备名 (CON, PRN, NUL, AUX, COM1-9, LPT1-9, CONIN$, CONOUT$)
    std::string upper = safe_name;
    for (char& c : upper) { if (c >= 'a' && c <= 'z') c -= 32; }
    if (upper == "CON" || upper == "PRN" || upper == "NUL" || upper == "AUX" ||
        upper == "CONIN$" || upper == "CONOUT$" ||
        (upper.size() >= 4 && upper.compare(0, 3, "COM") == 0 &&
         std::all_of(upper.begin() + 3, upper.end(), [](char c){ return c >= '0' && c <= '9'; })) ||
        (upper.size() >= 4 && upper.compare(0, 3, "LPT") == 0 &&
         std::all_of(upper.begin() + 3, upper.end(), [](char c){ return c >= '0' && c <= '9'; }))) {
        safe_name = "received_" + safe_name;
    }
    return safe_name;
}

std::string unique_filepath(const std::string& dir, const std::string& filename) {
    namespace fs = std::filesystem;
#ifdef _WIN32
    fs::path base(utf8_to_wpath(dir));
    base /= utf8_to_wpath(filename);
    if (!fs::exists(base)) return wpath_to_utf8(base.wstring());
    auto stem = fs::path(utf8_to_wpath(filename)).stem().wstring();
    auto ext = fs::path(utf8_to_wpath(filename)).extension().wstring();
    for (int i = 1; i < 10000; ++i) {
        fs::path candidate = base.parent_path() / (stem + L" (" + std::to_wstring(i) + L")" + ext);
        if (!fs::exists(candidate)) return wpath_to_utf8(candidate.wstring());
    }
    return wpath_to_utf8(base.wstring());
#else
    fs::path base(dir);
    base /= filename;
    if (!fs::exists(base)) return base.string();
    auto stem = fs::path(filename).stem().string();
    auto ext = fs::path(filename).extension().string();
    for (int i = 1; i < 10000; ++i) {
        fs::path candidate = base.parent_path() / (stem + " (" + std::to_string(i) + ")" + ext);
        if (!fs::exists(candidate)) return candidate.string();
    }
    return base.string();
#endif
}

} // namespace detail
} // namespace ft
