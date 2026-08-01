// FileTransfer GUI 主程序 - Win32 原生窗口界面
// 支持两种模式: 局域网直连 / 房间码中继 (跨局域网)
#include "file_transfer.h"
#include "relay.h"
#include "secret.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <stdexcept>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// 启用 XP+ 视觉样式
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' "\
    "processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' "\
    "language='*'\"")

// ========== 布局常量 ==========
static const int MARGIN = 12;          // 外边距
static const int LABEL_W = 120;        // 标签列宽度 (容纳中文标签)
static const int EDIT_H = 28;          // 输入框高度
static const int BTN_H = 34;           // 按钮高度
static const int BROWSE_W = 90;        // 浏览按钮宽度
// 最小窗口客户区尺寸
static const int MIN_W = 700;
static const int MIN_H = 820;

// ========== 传输模式 ==========
enum class TransferMode {
    LAN = 0,      // 局域网直连
    RELAY = 1,    // 房间码中继
};

// ========== 控件 ID ==========
enum ControlId {
    IDC_MODE_LAN = 1001,
    IDC_MODE_RELAY,
    // 局域网直连 - 发送
    IDC_SEND_PORT_EDIT,
    IDC_FILE_EDIT,
    IDC_FILE_BROWSE,
    IDC_SEND_BTN,
    // 局域网直连 - 接收
    IDC_RECV_PORT_EDIT,
    IDC_DIR_EDIT,
    IDC_DIR_BROWSE,
    IDC_RECV_BTN,
    // 中继 - 发送方
    IDC_RSEND_FILE_EDIT,
    IDC_RSEND_FILE_BROWSE,
    IDC_RSEND_BTN,
    IDC_RSEND_ADV_BTN,      // 高级设置 (自定义中继服务器)
    IDC_RSEND_CODE_EDIT,    // 显示生成的房间码
    // 中继 - 接收方
    IDC_RRECV_CODE_EDIT,    // 输入对方房间码
    IDC_RRECV_DIR_EDIT,
    IDC_RRECV_DIR_BROWSE,
    IDC_RRECV_BTN,
    // 公共
    IDC_PROGRESS,
    IDC_LOG,
    IDC_CANCEL_BTN,
};

// ========== 自定义消息 ==========
enum {
    WM_APP_UPDATE = WM_APP + 1,
    WM_APP_DONE,
    // 专用消息: 中继发送方收到房间码后, 把房间码显示到 UI
    WM_APP_ROOM_CODE,
};

// 系统托盘消息 + 菜单 ID
#define WM_TRAYICON (WM_APP + 100)
enum TrayMenuId {
    IDM_TRAY_SHOW = 2001,
    IDM_TRAY_RESET_CLOSE,
    IDM_TRAY_EXIT,
};

struct ProgressMsg {
    uint64_t done;
    uint64_t total;
    std::string text;
};

// ========== 全局应用上下文 ==========
struct AppContext {
    HWND hwnd = nullptr;
    HFONT hFont = nullptr;
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> busy{false};
    TransferMode mode = TransferMode::LAN;
    // 接收端 UDP 发现响应线程 (局域网模式)
    std::thread discovery_worker;
    std::atomic<bool> discovery_running{false};

    // 控件句柄
    HWND hTitle = nullptr;
    // 模式选择
    HWND hModeGroup = nullptr;
    HWND hModeLan = nullptr, hModeRelay = nullptr;

    // 局域网直连 - 发送区域
    HWND hSendGroup = nullptr;
    HWND hSendPortLbl = nullptr, hSendPortEdit = nullptr;
    HWND hFileLbl = nullptr, hFileEdit = nullptr;
    HWND hFileBrowse = nullptr, hSendBtn = nullptr;

    // 局域网直连 - 接收区域
    HWND hRecvGroup = nullptr;
    HWND hRecvPortLbl = nullptr, hRecvPortEdit = nullptr;
    HWND hDirLbl = nullptr, hDirEdit = nullptr;
    HWND hDirBrowse = nullptr, hRecvBtn = nullptr;

    // 中继 - 发送方
    HWND hRSendGroup = nullptr;
    HWND hRSendFileLbl = nullptr, hRSendFileEdit = nullptr;
    HWND hRSendFileBrowse = nullptr, hRSendBtn = nullptr;
    HWND hRSendAdvBtn = nullptr;  // 高级设置按钮
    HWND hRSendCodeLbl = nullptr, hRSendCodeEdit = nullptr;

    // 自定义中继服务器地址 (用户通过高级设置填入, 为空则使用内置地址)
    std::string custom_relay_host;
    unsigned short custom_relay_port = 0;
    bool use_custom_relay = false;

    // 进度消息节流时间戳 (避免 static 变量在多线程下的问题)
    std::chrono::steady_clock::time_point last_progress_tick;

    // 中继 - 接收方
    HWND hRRecvGroup = nullptr;
    HWND hRRecvCodeLbl = nullptr, hRRecvCodeEdit = nullptr;
    HWND hRRecvDirLbl = nullptr, hRRecvDirEdit = nullptr;
    HWND hRRecvDirBrowse = nullptr, hRRecvBtn = nullptr;

    HWND hProgress = nullptr, hLog = nullptr, hCancelBtn = nullptr, hLogLbl = nullptr;

    // 系统托盘 + 关闭行为偏好
    HINSTANCE hInst = nullptr;
    NOTIFYICONDATAW nid = {};
    bool tray_created = false;
    bool force_quit = false;       // 托盘"退出"触发, 跳过关闭对话框
    int close_action = 0;          // 0=询问, 1=最小化到托盘, 2=退出
};

static AppContext g_ctx;

// 节流发送进度消息 (100ms 间隔, 状态消息和完成消息立即发送)
// 避免 1GB 文件产生 1024 次 PostMessage + new/delete 导致 UI 卡顿
// last_tick 存储在 g_ctx 中, 避免多线程共享 static 变量的潜在问题
static void post_progress(uint64_t done, uint64_t total, const std::string& msg) {
    bool is_status = (total == 0);                        // 状态消息立即发送
    bool is_done = (total > 0 && done >= total);          // 完成消息立即发送
    if (!is_status && !is_done) {
        auto now = std::chrono::steady_clock::now();
        if (now - g_ctx.last_progress_tick < std::chrono::milliseconds(100)) return;
        g_ctx.last_progress_tick = now;
    } else {
        g_ctx.last_progress_tick = std::chrono::steady_clock::now();
    }
    auto* pm = new ProgressMsg{done, total, msg};
    PostMessageW(g_ctx.hwnd, WM_APP_UPDATE, 0, (LPARAM)pm);
}

// ========== 字符串编码转换 ==========
static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len,
                        nullptr, nullptr);
    return s;
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

static std::wstring GetTextW(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return {};
    std::wstring w(len, 0);
    GetWindowTextW(hEdit, &w[0], len + 1);
    return w;
}

static void AppendLog(const std::wstring& text) {
    int len = GetWindowTextLengthW(g_ctx.hLog);
    SendMessageW(g_ctx.hLog, EM_SETSEL, len, len);
    SendMessageW(g_ctx.hLog, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

// ========== 创建控件 ==========
static HWND CreateCtrl(HWND parent, const wchar_t* cls, const wchar_t* text,
                       DWORD style, int x, int y, int w, int h, int id) {
    HWND hwndCtrl = CreateWindowExW(0, cls, text,
                             WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id,
                             nullptr, nullptr);
    if (hwndCtrl && g_ctx.hFont) SendMessageW(hwndCtrl, WM_SETFONT, (WPARAM)g_ctx.hFont, TRUE);
    return hwndCtrl;
}

// 显示/隐藏一组控件 (用于模式切换)
static void ShowGroup(const std::initializer_list<HWND>& ctrls, BOOL show) {
    for (HWND h : ctrls) {
        if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    }
}

// ========== 布局: 根据客户区大小重排所有控件 ==========
static void DoLayout(int cx, int cy) {
    int x = MARGIN;
    int w = cx - MARGIN * 2;       // 内容区宽度

    // 标题
    MoveWindow(g_ctx.hTitle, x, 10, w, 26, TRUE);

    // ===== 模式选择区 =====
    int y = 40;
    int modeH = 50;
    MoveWindow(g_ctx.hModeGroup, x, y, w, modeH, TRUE);
    int ym = y + 22;
    // 两个 radio 并排
    MoveWindow(g_ctx.hModeLan, x + 14, ym, 130, 22, TRUE);
    MoveWindow(g_ctx.hModeRelay, x + 14 + 130 + 20, ym, 200, 22, TRUE);

    // ===== 局域网直连 - 发送区域 =====
    int yS = y + modeH + 8;
    int sendGroupH = 160;
    MoveWindow(g_ctx.hSendGroup, x, yS, w, sendGroupH, TRUE);
    int y2 = yS + 34;
    // 端口 (左上角, 与下方文件路径对齐)
    int portEditW = 70;
    MoveWindow(g_ctx.hSendPortLbl, x + 12, y2 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hSendPortEdit, x + LABEL_W, y2, portEditW, EDIT_H, TRUE);

    int y3 = y2 + EDIT_H + 14;
    int fileEditW = w - LABEL_W - BROWSE_W - 18;
    MoveWindow(g_ctx.hFileLbl, x + 12, y3 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hFileEdit, x + LABEL_W, y3, fileEditW, EDIT_H, TRUE);
    MoveWindow(g_ctx.hFileBrowse, x + LABEL_W + fileEditW + 8, y3 - 1, BROWSE_W, EDIT_H + 2, TRUE);

    int y4 = y3 + EDIT_H + 16;
    MoveWindow(g_ctx.hSendBtn, x + LABEL_W, y4, 140, BTN_H, TRUE);

    // ===== 局域网直连 - 接收区域 =====
    int yR = yS + sendGroupH + 8;
    int recvGroupH = 160;
    MoveWindow(g_ctx.hRecvGroup, x, yR, w, recvGroupH, TRUE);
    int yR2 = yR + 34;
    MoveWindow(g_ctx.hRecvPortLbl, x + 12, yR2 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hRecvPortEdit, x + LABEL_W, yR2, portEditW, EDIT_H, TRUE);

    int yR3 = yR2 + EDIT_H + 14;
    int dirEditW = w - LABEL_W - BROWSE_W - 18;
    MoveWindow(g_ctx.hDirLbl, x + 12, yR3 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hDirEdit, x + LABEL_W, yR3, dirEditW, EDIT_H, TRUE);
    MoveWindow(g_ctx.hDirBrowse, x + LABEL_W + dirEditW + 8, yR3 - 1, BROWSE_W, EDIT_H + 2, TRUE);

    int yR4 = yR3 + EDIT_H + 16;
    MoveWindow(g_ctx.hRecvBtn, x + LABEL_W, yR4, 150, BTN_H, TRUE);

    // ===== 中继 - 发送方区域 =====
    int yRS = yS;  // 与 LAN 发送区同位置 (互斥显示)
    int rSendGroupH = 160;
    MoveWindow(g_ctx.hRSendGroup, x, yRS, w, rSendGroupH, TRUE);
    int yRS2 = yRS + 34;
    int rFileEditW = w - LABEL_W - BROWSE_W - 18;
    MoveWindow(g_ctx.hRSendFileLbl, x + 12, yRS2 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hRSendFileEdit, x + LABEL_W, yRS2, rFileEditW, EDIT_H, TRUE);
    MoveWindow(g_ctx.hRSendFileBrowse, x + LABEL_W + rFileEditW + 8, yRS2 - 1, BROWSE_W, EDIT_H + 2, TRUE);

    int yRS3 = yRS2 + EDIT_H + 14;
    int codeShowW = 200;
    MoveWindow(g_ctx.hRSendCodeLbl, x + 12, yRS3 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hRSendCodeEdit, x + LABEL_W, yRS3, codeShowW, EDIT_H, TRUE);

    int yRS4 = yRS3 + EDIT_H + 16;
    int rSendBtnW = 160;
    int rAdvBtnW = 90;
    MoveWindow(g_ctx.hRSendBtn, x + LABEL_W, yRS4, rSendBtnW, BTN_H, TRUE);
    MoveWindow(g_ctx.hRSendAdvBtn, x + LABEL_W + rSendBtnW + 10, yRS4, rAdvBtnW, BTN_H, TRUE);

    // ===== 中继 - 接收方区域 =====
    int yRR = yR;  // 与 LAN 接收区同位置 (互斥显示)
    int rRecvGroupH = 160;
    MoveWindow(g_ctx.hRRecvGroup, x, yRR, w, rRecvGroupH, TRUE);
    int yRR2 = yRR + 34;
    int codeInpW = 200;
    MoveWindow(g_ctx.hRRecvCodeLbl, x + 12, yRR2 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hRRecvCodeEdit, x + LABEL_W, yRR2, codeInpW, EDIT_H, TRUE);

    int yRR3 = yRR2 + EDIT_H + 14;
    int rDirEditW = w - LABEL_W - BROWSE_W - 18;
    MoveWindow(g_ctx.hRRecvDirLbl, x + 12, yRR3 + 4, LABEL_W - 12, 20, TRUE);
    MoveWindow(g_ctx.hRRecvDirEdit, x + LABEL_W, yRR3, rDirEditW, EDIT_H, TRUE);
    MoveWindow(g_ctx.hRRecvDirBrowse, x + LABEL_W + rDirEditW + 8, yRR3 - 1, BROWSE_W, EDIT_H + 2, TRUE);

    int yRR4 = yRR3 + EDIT_H + 16;
    MoveWindow(g_ctx.hRRecvBtn, x + LABEL_W, yRR4, 200, BTN_H, TRUE);

    // ===== 进度条 + 取消按钮 =====
    int yP = yR + (std::max)(recvGroupH, rRecvGroupH) + 12;
    MoveWindow(g_ctx.hProgress, x, yP, w, 24, TRUE);
    int cancelW = 110;
    MoveWindow(g_ctx.hCancelBtn, x + (w - cancelW) / 2, yP + 34, cancelW, BTN_H - 4, TRUE);

    // ===== 日志区 (填充剩余空间) =====
    int yL = yP + 34 + BTN_H - 4 + 10;
    MoveWindow(g_ctx.hLogLbl, x, yL, 80, 20, TRUE);
    int yLog = yL + 24;
    int logH = cy - yLog - MARGIN;
    if (logH < 60) logH = 60;
    MoveWindow(g_ctx.hLog, x, yLog, w, logH, TRUE);
}

// ========== 模式切换: 显示/隐藏对应控件 ==========
static void ApplyModeVisibility() {
    bool lan = (g_ctx.mode == TransferMode::LAN);
    // LAN 直连区
    ShowGroup({
        g_ctx.hSendGroup,
        g_ctx.hSendPortLbl, g_ctx.hSendPortEdit, g_ctx.hFileLbl, g_ctx.hFileEdit,
        g_ctx.hFileBrowse, g_ctx.hSendBtn,
        g_ctx.hRecvGroup, g_ctx.hRecvPortLbl, g_ctx.hRecvPortEdit,
        g_ctx.hDirLbl, g_ctx.hDirEdit, g_ctx.hDirBrowse, g_ctx.hRecvBtn,
    }, lan ? TRUE : FALSE);

    // 中继区
    ShowGroup({
        g_ctx.hRSendGroup, g_ctx.hRSendFileLbl, g_ctx.hRSendFileEdit,
        g_ctx.hRSendFileBrowse, g_ctx.hRSendBtn, g_ctx.hRSendAdvBtn,
        g_ctx.hRSendCodeLbl, g_ctx.hRSendCodeEdit,
        g_ctx.hRRecvGroup, g_ctx.hRRecvCodeLbl, g_ctx.hRRecvCodeEdit,
        g_ctx.hRRecvDirLbl, g_ctx.hRRecvDirEdit, g_ctx.hRRecvDirBrowse, g_ctx.hRRecvBtn,
    }, lan ? FALSE : TRUE);

    // 强制重新布局
    RECT rc;
    GetClientRect(g_ctx.hwnd, &rc);
    DoLayout(rc.right, rc.bottom);
}

// ========== 启用/禁用控件 ==========
static void SetTransferControls(BOOL enabled) {
    // 模式切换始终禁用 (传输中不允许切换)
    EnableWindow(g_ctx.hModeLan, enabled);
    EnableWindow(g_ctx.hModeRelay, enabled);

    EnableWindow(g_ctx.hSendBtn, enabled);
    EnableWindow(g_ctx.hRecvBtn, enabled);
    EnableWindow(g_ctx.hSendPortEdit, enabled);
    EnableWindow(g_ctx.hFileEdit, enabled);
    EnableWindow(g_ctx.hFileBrowse, enabled);
    EnableWindow(g_ctx.hRecvPortEdit, enabled);
    EnableWindow(g_ctx.hDirEdit, enabled);
    EnableWindow(g_ctx.hDirBrowse, enabled);

    EnableWindow(g_ctx.hRSendBtn, enabled);
    EnableWindow(g_ctx.hRSendAdvBtn, enabled);
    EnableWindow(g_ctx.hRSendFileEdit, enabled);
    EnableWindow(g_ctx.hRSendFileBrowse, enabled);
    EnableWindow(g_ctx.hRRecvBtn, enabled);
    EnableWindow(g_ctx.hRRecvCodeEdit, enabled);
    EnableWindow(g_ctx.hRRecvDirEdit, enabled);
    EnableWindow(g_ctx.hRRecvDirBrowse, enabled);

    EnableWindow(g_ctx.hCancelBtn, !enabled);
}

// ========== 文件浏览 ==========
static void BrowseFile(HWND target_edit) {
    wchar_t buf[MAX_PATH] = {0};
    GetWindowTextW(target_edit, buf, MAX_PATH);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_ctx.hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"选择要发送的文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(target_edit, buf);
}

static void BrowseFolder(HWND target_edit) {
    wchar_t buf[MAX_PATH] = {0};
    BROWSEINFOW bi = {};
    bi.hwndOwner = g_ctx.hwnd;
    bi.pszDisplayName = buf;
    bi.lpszTitle = L"选择保存目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH] = {0};
        if (SHGetPathFromIDListW(pidl, path)) SetWindowTextW(target_edit, path);
        CoTaskMemFree(pidl);
    }
}

static unsigned short ParsePort(const std::wstring& s, bool& ok) {
    ok = true;
    if (s.empty()) return ft::DEFAULT_PORT;
    for (wchar_t c : s) {
        if (c < L'0' || c > L'9') { ok = false; return 0; }
    }
    long v = _wtol(s.c_str());
    if (v < 1 || v > 65535) { ok = false; return 0; }
    return (unsigned short)v;
}

// ========== 工作线程 ==========
// LAN 直连
static void TransferThread_LAN(bool is_send, std::string ip,
                                unsigned short port, std::string path) {
    auto cb = [](uint64_t done, uint64_t total, const std::string& msg) -> bool {
        post_progress(done, total, msg);
        return !g_ctx.cancel.load();
    };

    if (is_send && ip.empty()) {
        // 发送模式: 自动发现局域网内的接收端
        cb(0, 0, "[信息] 正在搜索局域网内的接收端...");
        auto peers = ft::discover_peers(port, 1500);
        if (peers.empty()) {
            cb(0, 0, "[错误] 未发现局域网内的接收端 (请确认接收方已启动并使用相同端口)");
            PostMessageW(g_ctx.hwnd, WM_APP_DONE, 1, 0);
            return;
        }
        if (peers.size() > 1) {
            std::string msg = "[信息] 发现 " + std::to_string(peers.size())
                            + " 个接收端, 将连接第一个: " + peers[0].first;
            msg += "\n[信息] 其他接收端:";
            for (std::size_t i = 1; i < peers.size(); ++i) {
                msg += "\n  " + peers[i].first + ":" + std::to_string(peers[i].second);
            }
            cb(0, 0, msg);
        } else {
            cb(0, 0, "[信息] 发现接收端: " + peers[0].first
                  + ":" + std::to_string(peers[0].second));
        }
        ip = peers[0].first;
    }

    int ret = is_send ? ft::send_file(ip, port, path, cb)
                      : ft::recv_file(port, path, cb);
    PostMessageW(g_ctx.hwnd, WM_APP_DONE, (WPARAM)ret, 0);
}

// 中继发送
static void TransferThread_RelaySend(std::string host, unsigned short port, std::string path) {
    static const std::string kCodePrefix = "[房间码] ";
    auto cb = [](uint64_t done, uint64_t total, const std::string& msg) -> bool {
        // 房间码通过特殊前缀识别, 单独发送一条 WM_APP_ROOM_CODE
        if (msg.rfind(kCodePrefix, 0) == 0) {
            std::string code = msg.substr(kCodePrefix.size());
            PostMessageW(g_ctx.hwnd, WM_APP_ROOM_CODE, 0, (LPARAM)new std::string(code));
        }
        post_progress(done, total, msg);
        return !g_ctx.cancel.load();
    };
    int ret = ft::relay_send_file(host, port, path, cb);
    PostMessageW(g_ctx.hwnd, WM_APP_DONE, (WPARAM)ret, 0);
}

// 中继接收
static void TransferThread_RelayRecv(std::string host, unsigned short port,
                                      std::string code, std::string dir) {
    auto cb = [](uint64_t done, uint64_t total, const std::string& msg) -> bool {
        post_progress(done, total, msg);
        return !g_ctx.cancel.load();
    };
    int ret = ft::relay_recv_file(host, port, code, dir, cb);
    PostMessageW(g_ctx.hwnd, WM_APP_DONE, (WPARAM)ret, 0);
}

// ========== 关闭行为偏好 (注册表读写) ==========
static const wchar_t* REG_KEY = L"Software\\FileTransfer";

static int LoadCloseAction() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(hKey, L"CloseAction", nullptr, nullptr,
                             (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (int)val;
        }
        RegCloseKey(hKey);
    }
    return 0;  // 默认: 询问
}

static void SaveCloseAction(int action) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD val = (DWORD)action;
        RegSetValueExW(hKey, L"CloseAction", 0, REG_DWORD,
                       (LPBYTE)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}

// ========== 自定义中继服务器设置持久化 (注册表) ==========
static void LoadCustomRelay() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        // 读取 use_custom_relay (DWORD)
        DWORD use_custom = 0, size = sizeof(use_custom);
        if (RegQueryValueExW(hKey, L"UseCustomRelay", nullptr, nullptr,
                             (LPBYTE)&use_custom, &size) == ERROR_SUCCESS && use_custom) {
            // 读取 host (REG_SZ)
            wchar_t host_buf[256] = {0};
            DWORD host_size = sizeof(host_buf);
            if (RegQueryValueExW(hKey, L"CustomRelayHost", nullptr, nullptr,
                                 (LPBYTE)host_buf, &host_size) == ERROR_SUCCESS && host_size > 0) {
                std::string host = wide_to_utf8(host_buf);
                if (!host.empty()) {
                    DWORD port = 0; DWORD port_size = sizeof(port);
                    if (RegQueryValueExW(hKey, L"CustomRelayPort", nullptr, nullptr,
                                         (LPBYTE)&port, &port_size) == ERROR_SUCCESS &&
                        port >= 1 && port <= 65535) {
                        g_ctx.custom_relay_host = host;
                        g_ctx.custom_relay_port = static_cast<unsigned short>(port);
                        g_ctx.use_custom_relay = true;
                    }
                }
            }
        }
        RegCloseKey(hKey);
    }
}

static void SaveCustomRelay() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD use_custom = g_ctx.use_custom_relay ? 1 : 0;
        RegSetValueExW(hKey, L"UseCustomRelay", 0, REG_DWORD,
                       (LPBYTE)&use_custom, sizeof(use_custom));
        if (g_ctx.use_custom_relay) {
            std::wstring host_w = utf8_to_wide(g_ctx.custom_relay_host);
            RegSetValueExW(hKey, L"CustomRelayHost", 0, REG_SZ,
                           (LPBYTE)host_w.c_str(),
                           static_cast<DWORD>((host_w.size() + 1) * sizeof(wchar_t)));
            DWORD port = g_ctx.custom_relay_port;
            RegSetValueExW(hKey, L"CustomRelayPort", 0, REG_DWORD,
                           (LPBYTE)&port, sizeof(port));
        }
        RegCloseKey(hKey);
    }
}

// ========== 系统托盘 ==========
static void TrayCreate() {
    if (g_ctx.tray_created || !g_ctx.hwnd || !g_ctx.hInst) return;
    g_ctx.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_ctx.nid.hWnd = g_ctx.hwnd;
    g_ctx.nid.uID = 1;
    g_ctx.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_ctx.nid.uCallbackMessage = WM_TRAYICON;
    g_ctx.nid.hIcon = LoadIconW(g_ctx.hInst, MAKEINTRESOURCEW(1));
    wcscpy_s(g_ctx.nid.szTip, L"FileTransfer - 文件传输");
    Shell_NotifyIconW(NIM_ADD, &g_ctx.nid);
    g_ctx.tray_created = true;
}

static void TrayDelete() {
    if (!g_ctx.tray_created) return;
    Shell_NotifyIconW(NIM_DELETE, &g_ctx.nid);
    g_ctx.tray_created = false;
}

static void ShowTrayMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"显示主窗口");
    if (g_ctx.close_action != 0) {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESET_CLOSE, L"重置关闭选择");
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"退出程序");

    POINT pt;
    GetCursorPos(&pt);
    // 必须前置窗口, 否则菜单点击后不会自动消失
    SetForegroundWindow(hWnd);
    int cmd = (int)TrackPopupMenu(hMenu,
        TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
        pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_TRAY_SHOW:
        ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
        break;
    case IDM_TRAY_RESET_CLOSE:
        g_ctx.close_action = 0;
        SaveCloseAction(0);
        MessageBoxW(hWnd, L"已重置关闭选择, 下次关闭窗口将再次询问。",
                    L"提示", MB_OK | MB_ICONINFORMATION);
        break;
    case IDM_TRAY_EXIT:
        g_ctx.force_quit = true;
        PostMessageW(hWnd, WM_CLOSE, 0, 0);
        break;
    }
}

// ========== 关闭选择对话框 (自绘美化版) ==========
// 返回: 0=取消(不关闭), 1=最小化到托盘, 2=退出

// 对话框控件 ID
enum CloseDlgCtrlId {
    IDC_CLOSE_MIN = 3001,
    IDC_CLOSE_EXIT,
    IDC_CLOSE_REMEMBER,
    IDC_CLOSE_ICON,
    IDC_CLOSE_TITLE,
    IDC_CLOSE_DESC,
    IDC_CLOSE_CANCEL,
    IDC_CLOSE_X,       // 右上角叉号按钮
};

// 对话框状态(按钮悬停/按下)
struct DlgState {
    HWND hwnd = nullptr;
    HWND hBtnMin = nullptr;
    HWND hBtnExit = nullptr;
    HWND hBtnCancel = nullptr;
    HWND hBtnX = nullptr;        // 叉号按钮
    HWND hChkRemember = nullptr;
    HFONT hFontTitle = nullptr;
    HFONT hFontBody = nullptr;
    HFONT hFontBtn = nullptr;
    HFONT hFontX = nullptr;
    int result = 0;
    bool remember = false;
    bool hoverMin = false, hoverExit = false, hoverCancel = false, hoverX = false;
    bool pressedMin = false, pressedExit = false, pressedCancel = false, pressedX = false;
};

static DlgState g_dlg;

// 绘制渐变背景 (蓝→紫, 与程序图标配色一致)
static void PaintDlgBackground(HWND hWnd) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    HDC hdc = GetDC(hWnd);
    
    int w = rc.right, h = rc.bottom;
    
    // 主背景: 淡蓝灰色
    HBRUSH hMainBrush = CreateSolidBrush(RGB(245, 247, 252));
    FillRect(hdc, &rc, hMainBrush);
    DeleteObject(hMainBrush);
    
    // 顶部标题栏区域 (深蓝紫) - 38px
    RECT titleRc = { 0, 0, w, 38 };
    HBRUSH hTitleBrush = CreateSolidBrush(RGB(120, 130, 220));
    FillRect(hdc, &titleRc, hTitleBrush);
    DeleteObject(hTitleBrush);
    
    // 底部装饰条
    RECT bottomRc = { 0, h - 3, w, h };
    HBRUSH hBottomBrush = CreateSolidBrush(RGB(200, 210, 230));
    FillRect(hdc, &bottomRc, hBottomBrush);
    DeleteObject(hBottomBrush);
    
    ReleaseDC(hWnd, hdc);
}

// 绘制圆角区域
static void SetRoundedRegion(HWND hWnd, int radius) {
    RECT rc;
    GetWindowRect(hWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HRGN hRgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
    SetWindowRgn(hWnd, hRgn, TRUE);
    // SetWindowRgn 会拷贝区域, 可删除原 RGN
    DeleteObject(hRgn);
}

// 绘制文本
static void PaintDlgText(HWND hWnd) {
    HDC hdc = GetDC(hWnd);
    RECT rc;
    GetClientRect(hWnd, &rc);
    
    int w = rc.right;
    
    // 标题 (在标题栏中, 左对齐与正文对齐)
    SelectObject(hdc, g_dlg.hFontTitle);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    TextOutW(hdc, 32, 5, L"关闭窗口", 8);
    
    // 主内容区标题 - 紧凑布局
    SelectObject(hdc, g_dlg.hFontBody);
    SetTextColor(hdc, RGB(40, 40, 60));
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    
    RECT titleRc = { 32, 44, w - 32, 84 };
    DrawTextW(hdc, L"您希望最小化到系统托盘还是完全退出程序？", -1, &titleRc, DT_LEFT | DT_WORDBREAK);
    
    RECT descRc = { 32, 86, w - 32, 124 };
    SetTextColor(hdc, RGB(110, 110, 130));
    DrawTextW(hdc, L"最小化到托盘后, 程序将在后台继续运行。\n您可以右键托盘图标选择显示或退出。", -1, &descRc, DT_LEFT | DT_WORDBREAK);
    
    ReleaseDC(hWnd, hdc);
}

// 绘制按钮
static void PaintDlgButton(HWND hBtn, bool primary, bool hover, bool pressed) {
    HDC hdc = GetDC(hBtn);
    RECT rc;
    GetClientRect(hBtn, &rc);
    
    // 确定颜色
    COLORREF bgColor;
    COLORREF borderColor;
    COLORREF textColor;
    
    if (primary) {
        if (pressed) {
            bgColor = RGB(80, 100, 200);
            borderColor = RGB(80, 100, 200);  // 边框与背景同色
        } else if (hover) {
            bgColor = RGB(100, 120, 230);
            borderColor = RGB(100, 120, 230);
        } else {
            bgColor = RGB(120, 140, 240);
            borderColor = RGB(120, 140, 240);  // 无边框效果
        }
        textColor = RGB(255, 255, 255);
    } else {
        if (pressed) {
            bgColor = RGB(220, 225, 240);
            borderColor = RGB(200, 205, 220);
        } else if (hover) {
            bgColor = RGB(235, 240, 250);
            borderColor = RGB(215, 220, 235);
        } else {
            bgColor = RGB(255, 255, 255);
            borderColor = RGB(220, 225, 235);
        }
        textColor = RGB(60, 60, 80);
    }
    
    // 绘制圆角背景 (用 RoundRect 一次完成填充+边框, 避免白边)
    int radius = 8;
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
    
    // 绘制文字 (使用 DrawText 实现真正的居中)
    SelectObject(hdc, g_dlg.hFontBtn);
    SetTextColor(hdc, textColor);
    SetBkMode(hdc, TRANSPARENT);
    
    const wchar_t* text = nullptr;
    int id = GetDlgCtrlID(hBtn);
    if (id == IDC_CLOSE_MIN) text = L"最小化到系统托盘";
    else if (id == IDC_CLOSE_EXIT) text = L"完全退出程序";
    else if (id == IDC_CLOSE_CANCEL) text = L"取消";
    
    if (text) {
        RECT textRc = rc;
        if (pressed) {
            OffsetRect(&textRc, 1, 1);
        }
        DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    
    ReleaseDC(hBtn, hdc);
}

// 自定义对话框过程
static LRESULT CALLBACK CloseDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_dlg.hwnd = hWnd;
        
        // 创建字体 - 更大字号
        g_dlg.hFontTitle = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        FF_DONTCARE, L"Microsoft YaHei UI");
        g_dlg.hFontBody = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       FF_DONTCARE, L"Microsoft YaHei UI");
        g_dlg.hFontBtn = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      FF_DONTCARE, L"Microsoft YaHei UI");
        g_dlg.hFontX = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    FF_DONTCARE, L"Microsoft YaHei UI");
        
        // 右上角叉号按钮 (红色圆形) - 28x28 正方形, 确保正圆
        g_dlg.hBtnX = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            390, 5, 28, 28, hWnd, (HMENU)IDC_CLOSE_X,
            nullptr, nullptr);
        // 设置按钮为圆形窗口区域
        RECT xRc;
        GetWindowRect(g_dlg.hBtnX, &xRc);
        HRGN hEllipseRgn = CreateEllipticRgn(0, 0, xRc.right - xRc.left, xRc.bottom - xRc.top);
        SetWindowRgn(g_dlg.hBtnX, hEllipseRgn, TRUE);
        DeleteObject(hEllipseRgn);
        
        // 创建复选框 (紧凑布局)
        g_dlg.hChkRemember = CreateWindowExW(0, L"BUTTON",
            L"  记住我的选择 (不再询问)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            28, 124, 300, 28, hWnd, (HMENU)IDC_CLOSE_REMEMBER,
            nullptr, nullptr);
        SendMessageW(g_dlg.hChkRemember, WM_SETFONT, (WPARAM)g_dlg.hFontBtn, TRUE);
        SetWindowTextW(g_dlg.hChkRemember, L"记住我的选择 (不再询问)");
        
        // 创建按钮 - 紧凑布局, 440px 宽度
        // 28 + 170 + 14 + 170 + 28 = 410 → 居中在 440 宽度内
        int btnW = 170, btnH = 46;
        int btnY = 160;
        int totalW = btnW * 2 + 14;
        int startX = (440 - totalW) / 2;
        g_dlg.hBtnMin = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, btnY, btnW, btnH, hWnd, (HMENU)IDC_CLOSE_MIN,
            nullptr, nullptr);
        g_dlg.hBtnExit = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + btnW + 14, btnY, btnW, btnH, hWnd, (HMENU)IDC_CLOSE_EXIT,
            nullptr, nullptr);
        
        // 设置圆角
        SetRoundedRegion(hWnd, 16);
        break;
    }
    
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // 背景由 WM_ERASEBKGND 处理
        EndPaint(hWnd, &ps);
        
        PaintDlgBackground(hWnd);
        PaintDlgText(hWnd);
        break;
    }
    
    case WM_ERASEBKGND:
        return 1;  // 阻止默认擦除, 用自定义绘制
    
    case WM_CTLCOLORBTN: {
        // 根据控件ID返回不同画刷, 消除白边
        HDC hdcBtn = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        int ctrlId = GetDlgCtrlID(hCtrl);
        
        SetBkMode(hdcBtn, TRANSPARENT);
        
        // 叉号按钮: 返回标题栏背景色, 让矩形背景与标题栏融合
        // 红色圆形完全由 WM_DRAWITEM 中的 Ellipse() 绘制
        if (ctrlId == IDC_CLOSE_X) {
            static HBRUSH hTitleBrush = CreateSolidBrush(RGB(120, 130, 220));
            return (INT_PTR)hTitleBrush;
        }
        // 普通按钮: 返回与对话框背景同色的画刷
        static HBRUSH hBgBrush = CreateSolidBrush(RGB(245, 247, 252));
        return (INT_PTR)hBgBrush;
    }
    
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlType == ODT_BUTTON) {
            int id = dis->CtlID;
            if (id == IDC_CLOSE_MIN) {
                bool hover = g_dlg.hoverMin, pressed = g_dlg.pressedMin;
                PaintDlgButton(g_dlg.hBtnMin, true, hover, pressed);
            } else if (id == IDC_CLOSE_EXIT) {
                bool hover = g_dlg.hoverExit, pressed = g_dlg.pressedExit;
                PaintDlgButton(g_dlg.hBtnExit, true, hover, pressed);
            } else if (id == IDC_CLOSE_X) {
                // 叉号按钮: 红色圆形背景 + 白色叉号
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                
                // 计算中心点和半径 (使用客户区坐标, 从 0,0 开始)
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;
                int cx = w / 2;
                int cy = h / 2;
                int radius = (w < h ? w : h) / 2 - 1;
                
                // 红色圆形背景 (悬停时变深红)
                COLORREF redClr = g_dlg.hoverX ? RGB(200, 50, 50) : RGB(220, 80, 80);
                HBRUSH hRed = CreateSolidBrush(redClr);
                HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hNullPen);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hRed);
                // 绘制圆形 (使用客户区相对坐标)
                Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
                SelectObject(hdc, hOldBrush);
                SelectObject(hdc, hOldPen);
                DeleteObject(hNullPen);
                DeleteObject(hRed);
                
                // 白色叉号 (根据半径自适应大小)
                int lineLen = radius * 3 / 5;  // 叉号线段长度
                HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                HPEN hOldPen2 = (HPEN)SelectObject(hdc, hPen);
                MoveToEx(hdc, cx - lineLen, cy - lineLen, nullptr);
                LineTo(hdc, cx + lineLen, cy + lineLen);
                MoveToEx(hdc, cx + lineLen, cy - lineLen, nullptr);
                LineTo(hdc, cx - lineLen, cy + lineLen);
                SelectObject(hdc, hOldPen2);
                DeleteObject(hPen);
            }
        }
        return 0;
    }
    
    case WM_MOUSEMOVE: {
        // 检测鼠标悬停
        POINT pt;
        GetCursorPos(&pt);
        HWND hOver = WindowFromPoint(pt);
        
        bool newHoverMin = (hOver == g_dlg.hBtnMin);
        bool newHoverExit = (hOver == g_dlg.hBtnExit);
        bool newHoverX = (hOver == g_dlg.hBtnX);
        
        if (newHoverMin != g_dlg.hoverMin) {
            g_dlg.hoverMin = newHoverMin;
            InvalidateRect(g_dlg.hBtnMin, nullptr, TRUE);
        }
        if (newHoverExit != g_dlg.hoverExit) {
            g_dlg.hoverExit = newHoverExit;
            InvalidateRect(g_dlg.hBtnExit, nullptr, TRUE);
        }
        if (newHoverX != g_dlg.hoverX) {
            g_dlg.hoverX = newHoverX;
            InvalidateRect(g_dlg.hBtnX, nullptr, TRUE);
        }
        break;
    }
    
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        
        if (code == BN_PUSHED || code == BN_CLICKED) {
            if (id == IDC_CLOSE_MIN) {
                g_dlg.result = 1;
                g_dlg.remember = (SendMessageW(g_dlg.hChkRemember, BM_GETCHECK, 0, 0) == BST_CHECKED);
                DestroyWindow(hWnd);
            } else if (id == IDC_CLOSE_EXIT) {
                g_dlg.result = 2;
                g_dlg.remember = (SendMessageW(g_dlg.hChkRemember, BM_GETCHECK, 0, 0) == BST_CHECKED);
                DestroyWindow(hWnd);
            } else if (id == IDC_CLOSE_X) {
                g_dlg.result = 0;  // 叉号 = 取消
                DestroyWindow(hWnd);
            }
        }
        break;
    }
    
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            g_dlg.result = 0;
            DestroyWindow(hWnd);
        } else if (wParam == VK_RETURN) {
            // 回车默认最小化
            g_dlg.result = 1;
            g_dlg.remember = (SendMessageW(g_dlg.hChkRemember, BM_GETCHECK, 0, 0) == BST_CHECKED);
            DestroyWindow(hWnd);
        }
        break;
    
    case WM_NCHITTEST: {
        // 获取鼠标相对客户区坐标
        LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            // 标题栏区域 (y < 38px) 返回 HTCAPTION 支持拖动
            if (pt.y < 38) {
                return HTCAPTION;
            }
        }
        return hit;
    }
    
    case WM_SIZE:
        // 窗口大小变化时重新设置圆角区域
        SetRoundedRegion(hWnd, 16);
        break;
    
    case WM_CLOSE:
        g_dlg.result = 0;
        DestroyWindow(hWnd);
        return 0;
    
    case WM_DESTROY:
        if (g_dlg.hFontTitle) DeleteObject(g_dlg.hFontTitle);
        if (g_dlg.hFontBody) DeleteObject(g_dlg.hFontBody);
        if (g_dlg.hFontBtn) DeleteObject(g_dlg.hFontBtn);
        if (g_dlg.hFontX) DeleteObject(g_dlg.hFontX);
        // 注意: 这里不调用 PostQuitMessage, 因为对话框是模态的
        // PostQuitMessage 会让主程序消息循环退出
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 显示关闭选择对话框 (美化版)
// 返回: 0=取消(不关闭), 1=最小化到托盘, 2=退出
static int ShowCloseChoiceDialog(HWND hWnd) {
    // 重置状态
    g_dlg = {};
    
    // 注册对话框窗口类
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CloseDlgProc;
        wc.hInstance = g_ctx.hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"FileTransferCloseDlg";
        RegisterClassExW(&wc);
        registered = true;
    }
    
    int dlgW = 440, dlgH = 240;
    
    // 计算居中位置
    RECT parentRc;
    GetWindowRect(hWnd, &parentRc);
    int x = parentRc.left + (parentRc.right - parentRc.left - dlgW) / 2;
    int y = parentRc.top + (parentRc.bottom - parentRc.top - dlgH) / 2;
    
    // 创建模态对话框 - 使用父子关系保持 z-order, 移除 DLGMODALFRAME 消除白边
    HWND hDlg = CreateWindowExW(
        0,
        L"FileTransferCloseDlg",
        L"",
        WS_POPUP | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hWnd, nullptr, g_ctx.hInst, nullptr);
    
    if (!hDlg) return 0;
    
    // 禁用父窗口 - 完全阻止用户与主窗口交互
    EnableWindow(hWnd, FALSE);
    
    // 显示对话框
    ShowWindow(hDlg, SW_SHOW);
    // 通过父子关系确保对话框在父窗口之上 (Windows 自动维护 owner z-order)
    SetWindowPos(hDlg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hDlg);
    
    // 模态消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hDlg)) break;
    }
    
    // 恢复父窗口
    EnableWindow(hWnd, TRUE);
    SetForegroundWindow(hWnd);
    
    // 根据结果保存偏好
    if (g_dlg.remember && g_dlg.result != 0) {
        g_ctx.close_action = g_dlg.result;
        SaveCloseAction(g_dlg.result);
    }
    
    return g_dlg.result;
}

// ========== 自定义中继服务器设置对话框 ==========

// 对话框控件 ID
enum AdvRelayCtrlId {
    IDC_AR_IP_LABEL = 3001,
    IDC_AR_IP_EDIT,
    IDC_AR_PORT_LABEL,
    IDC_AR_PORT_EDIT,
    IDC_AR_HINT,
    IDC_AR_OK,
    IDC_AR_CANCEL,
    IDC_AR_CLEAR,
};

// 全局对话框状态 (类似 g_dlg, 用于在 WndProc 和 Show 函数间传递数据)
struct AdvRelayDlgState {
    HWND hwnd = nullptr;
    HWND hIpEdit = nullptr;
    HWND hPortEdit = nullptr;
    bool confirmed = false;
    bool use_custom = false;
    std::string ip;
    unsigned short port = 0;
};
static AdvRelayDlgState g_adv;

// 高级设置对话框窗口过程
static LRESULT CALLBACK AdvRelayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 创建子控件
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateWindowExW(0, L"STATIC", L"中继服务器 IP:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 22, 120, 18, hWnd, (HMENU)IDC_AR_IP_LABEL, g_ctx.hInst, nullptr);

        g_adv.hIpEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            150, 20, 200, 22, hWnd, (HMENU)IDC_AR_IP_EDIT, g_ctx.hInst, nullptr);

        CreateWindowExW(0, L"STATIC", L"端口:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 52, 120, 18, hWnd, (HMENU)IDC_AR_PORT_LABEL, g_ctx.hInst, nullptr);

        g_adv.hPortEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            150, 50, 80, 22, hWnd, (HMENU)IDC_AR_PORT_EDIT, g_ctx.hInst, nullptr);

        // 提示文字
        CreateWindowExW(0, L"STATIC",
            L"点击\"恢复默认\"可使用内置中继服务器。\r\n发送方和接收方自动使用相同的中继服务器地址。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 82, 340, 32, hWnd, (HMENU)IDC_AR_HINT, g_ctx.hInst, nullptr);

        CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
            80, 145, 80, 28, hWnd, (HMENU)IDC_AR_OK, g_ctx.hInst, nullptr);

        CreateWindowExW(0, L"BUTTON", L"恢复默认",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            175, 145, 80, 28, hWnd, (HMENU)IDC_AR_CLEAR, g_ctx.hInst, nullptr);

        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            270, 145, 80, 28, hWnd, (HMENU)IDC_AR_CANCEL, g_ctx.hInst, nullptr);

        // 设置字体
        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
        }, (LPARAM)hFont);

        // 填充当前值
        if (g_ctx.use_custom_relay) {
            SetWindowTextW(g_adv.hIpEdit, utf8_to_wide(g_ctx.custom_relay_host).c_str());
            SetWindowTextW(g_adv.hPortEdit, std::to_wstring(g_ctx.custom_relay_port).c_str());
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_AR_OK) {
            wchar_t wip[256] = {0};
            wchar_t wport[16] = {0};
            GetWindowTextW(g_adv.hIpEdit, wip, 256);
            GetWindowTextW(g_adv.hPortEdit, wport, 16);
            std::string ip = wide_to_utf8(wip);
            while (!ip.empty() && ip.back() == ' ') ip.pop_back();
            while (!ip.empty() && ip.front() == ' ') ip.erase(ip.begin());
            if (ip.empty()) {
                MessageBoxW(hWnd, L"请输入中继服务器 IP 地址", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            unsigned short port = 0;
            try {
                long p = std::stol(wide_to_utf8(wport));
                if (p < 1 || p > 65535) throw std::out_of_range("port");
                port = static_cast<unsigned short>(p);
            } catch (...) {
                MessageBoxW(hWnd, L"端口号必须是 1-65535 范围内的数字", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            g_adv.ip = ip;
            g_adv.port = port;
            g_adv.use_custom = true;
            g_adv.confirmed = true;
            DestroyWindow(hWnd);
            return 0;
        }
        if (id == IDC_AR_CLEAR) {
            g_adv.use_custom = false;
            g_adv.confirmed = true;
            DestroyWindow(hWnd);
            return 0;
        }
        if (id == IDC_AR_CANCEL || id == IDCANCEL) {
            g_adv.confirmed = false;
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        g_adv.confirmed = false;
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        g_adv.hwnd = nullptr;
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 弹出自定义中继服务器设置对话框 (模态)
static bool ShowAdvRelayDialog(HWND hParent) {
    // 重置状态
    g_adv = {};

    // 注册窗口类
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AdvRelayWndProc;
        wc.hInstance = g_ctx.hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"FileTransferAdvRelayDlg";
        RegisterClassExW(&wc);
        registered = true;
    }

    int dlgW = 380, dlgH = 220;

    // 计算居中位置
    RECT parentRc;
    GetWindowRect(hParent, &parentRc);
    int x = parentRc.left + (parentRc.right - parentRc.left - dlgW) / 2;
    int y = parentRc.top + (parentRc.bottom - parentRc.top - dlgH) / 2;

    // 创建窗口
    HWND hDlg = CreateWindowExW(0,
        L"FileTransferAdvRelayDlg",
        L"高级设置 - 自定义中继服务器",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hParent, nullptr, g_ctx.hInst, nullptr);

    if (!hDlg) return false;
    g_adv.hwnd = hDlg;

    // 禁用父窗口
    EnableWindow(hParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    SetForegroundWindow(hDlg);

    // 模态消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hDlg)) break;
    }

    // 恢复父窗口
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    // 将结果写回 g_ctx
    if (g_adv.confirmed) {
        if (g_adv.use_custom) {
            g_ctx.custom_relay_host = g_adv.ip;
            g_ctx.custom_relay_port = g_adv.port;
            g_ctx.use_custom_relay = true;
        } else {
            g_ctx.use_custom_relay = false;
            g_ctx.custom_relay_host.clear();
            g_ctx.custom_relay_port = 0;
        }
        SaveCustomRelay();  // 持久化到注册表, 重启后保留
    }
    return g_adv.confirmed;
}

// 获取当前生效的中继服务器地址 (自定义优先, 回退到内置)
static bool get_effective_relay_addr(std::string& host, unsigned short& port) {
    if (g_ctx.use_custom_relay && !g_ctx.custom_relay_host.empty()) {
        host = g_ctx.custom_relay_host;
        port = g_ctx.custom_relay_port;
        return true;
    }
    return ft::parse_relay_addr(host, port);
}

// ========== 窗口过程 ==========
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 创建字体 (18px, 约 13.5pt, 清晰易读)
        g_ctx.hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  FF_DONTCARE, L"Microsoft YaHei UI");

        // 标题
        g_ctx.hTitle = CreateCtrl(hWnd, L"static",
                   L"FileTransfer v0.0.3  -  文件传输 (局域网 / 房间码中继)",
                   SS_CENTER, 0, 0, 10, 10, 0);

        // ===== 模式选择区 =====
        g_ctx.hModeGroup = CreateCtrl(hWnd, L"button", L"传输模式",
                   BS_GROUPBOX, 0, 0, 10, 10, 0);
        g_ctx.hModeLan = CreateCtrl(hWnd, L"button", L"局域网直连",
                   BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 10, 10, IDC_MODE_LAN);
        g_ctx.hModeRelay = CreateCtrl(hWnd, L"button", L"房间码中继 (跨局域网)",
                   BS_AUTORADIOBUTTON, 0, 0, 10, 10, IDC_MODE_RELAY);
        // 默认选中局域网直连
        SendMessageW(g_ctx.hModeLan, BM_SETCHECK, BST_CHECKED, 0);

        // ===== 局域网直连 - 发送区域 =====
        g_ctx.hSendGroup = CreateCtrl(hWnd, L"button", L"发送文件 (自动发现接收端)",
                   BS_GROUPBOX, 0, 0, 10, 10, 0);
        g_ctx.hSendPortLbl = CreateCtrl(hWnd, L"static", L"端口:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hSendPortEdit = CreateCtrl(hWnd, L"edit", L"9090",
                   ES_AUTOHSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_SEND_PORT_EDIT);
        g_ctx.hFileLbl = CreateCtrl(hWnd, L"static", L"文件路径:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hFileEdit = CreateCtrl(hWnd, L"edit", L"",
                   ES_AUTOHSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_FILE_EDIT);
        g_ctx.hFileBrowse = CreateCtrl(hWnd, L"button", L"浏览...",
                   BS_PUSHBUTTON, 0, 0, 10, 10, IDC_FILE_BROWSE);
        g_ctx.hSendBtn = CreateCtrl(hWnd, L"button", L"发送",
                   BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 0, 0, 10, 10, IDC_SEND_BTN);

        // ===== 局域网直连 - 接收区域 =====
        g_ctx.hRecvGroup = CreateCtrl(hWnd, L"button", L"接收文件 (直连 - 服务端)",
                   BS_GROUPBOX, 0, 0, 10, 10, 0);
        g_ctx.hRecvPortLbl = CreateCtrl(hWnd, L"static", L"端口:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRecvPortEdit = CreateCtrl(hWnd, L"edit", L"9090",
                   ES_AUTOHSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_RECV_PORT_EDIT);
        g_ctx.hDirLbl = CreateCtrl(hWnd, L"static", L"保存目录:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hDirEdit = CreateCtrl(hWnd, L"edit", L"",
                   ES_AUTOHSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_DIR_EDIT);
        g_ctx.hDirBrowse = CreateCtrl(hWnd, L"button", L"浏览...",
                   BS_PUSHBUTTON, 0, 0, 10, 10, IDC_DIR_BROWSE);
        g_ctx.hRecvBtn = CreateCtrl(hWnd, L"button", L"开始接收",
                   BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 0, 0, 10, 10, IDC_RECV_BTN);

        // ===== 中继 - 发送方 =====
        g_ctx.hRSendGroup = CreateCtrl(hWnd, L"button", L"中继发送 (创建房间)",
                   BS_GROUPBOX, 0, 0, 10, 10, 0);
        g_ctx.hRSendFileLbl = CreateCtrl(hWnd, L"static", L"文件路径:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRSendFileEdit = CreateCtrl(hWnd, L"edit", L"",
                   ES_AUTOHSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_RSEND_FILE_EDIT);
        g_ctx.hRSendFileBrowse = CreateCtrl(hWnd, L"button", L"浏览...",
                   BS_PUSHBUTTON, 0, 0, 10, 10, IDC_RSEND_FILE_BROWSE);
        g_ctx.hRSendCodeLbl = CreateCtrl(hWnd, L"static", L"房间码:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRSendCodeEdit = CreateCtrl(hWnd, L"edit", L"(创建后显示)",
                   ES_AUTOHSCROLL | WS_BORDER | ES_READONLY, 0, 0, 10, 10, IDC_RSEND_CODE_EDIT);
        g_ctx.hRSendBtn = CreateCtrl(hWnd, L"button", L"创建房间并发送",
                   BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 0, 0, 10, 10, IDC_RSEND_BTN);
        g_ctx.hRSendAdvBtn = CreateCtrl(hWnd, L"button", L"高级设置",
                   BS_PUSHBUTTON, 0, 0, 10, 10, IDC_RSEND_ADV_BTN);

        // ===== 中继 - 接收方 =====
        g_ctx.hRRecvGroup = CreateCtrl(hWnd, L"button", L"中继接收 (输入房间码)",
                   BS_GROUPBOX, 0, 0, 10, 10, 0);
        g_ctx.hRRecvCodeLbl = CreateCtrl(hWnd, L"static", L"房间码:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRRecvCodeEdit = CreateCtrl(hWnd, L"edit", L"",
                   ES_AUTOHSCROLL | WS_BORDER | ES_CENTER, 0, 0, 10, 10, IDC_RRECV_CODE_EDIT);
        g_ctx.hRRecvDirLbl = CreateCtrl(hWnd, L"static", L"保存目录:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRRecvDirEdit = CreateCtrl(hWnd, L"edit", L"",
                   ES_AUTOHSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_RRECV_DIR_EDIT);
        g_ctx.hRRecvDirBrowse = CreateCtrl(hWnd, L"button", L"浏览...",
                   BS_PUSHBUTTON, 0, 0, 10, 10, IDC_RRECV_DIR_BROWSE);
        g_ctx.hRRecvBtn = CreateCtrl(hWnd, L"button", L"加入房间并接收",
                   BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 0, 0, 10, 10, IDC_RRECV_BTN);

        // ===== 进度 + 取消 =====
        g_ctx.hProgress = CreateCtrl(hWnd, PROGRESS_CLASSW, L"",
                   PBS_SMOOTH, 0, 0, 10, 10, IDC_PROGRESS);
        SendMessageW(g_ctx.hProgress, PBM_SETRANGE32, 0, 100);
        g_ctx.hCancelBtn = CreateCtrl(hWnd, L"button", L"取消传输",
                   BS_PUSHBUTTON, 0, 0, 10, 10, IDC_CANCEL_BTN);
        EnableWindow(g_ctx.hCancelBtn, FALSE);

        // ===== 日志 =====
        g_ctx.hLogLbl = CreateCtrl(hWnd, L"static", L"状态日志:", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hLog = CreateCtrl(hWnd, L"edit", L"",
                   ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
                   WS_VSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_LOG);

        // 占位提示
        SendMessageW(g_ctx.hFileEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"选择要发送的文件");
        SendMessageW(g_ctx.hDirEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"留空则保存到程序所在目录");
        SendMessageW(g_ctx.hRSendFileEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"选择要发送的文件");
        SendMessageW(g_ctx.hRRecvCodeEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"6 位字母数字");
        SendMessageW(g_ctx.hRRecvDirEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"留空则保存到程序所在目录");

        // 初始模式可见性
        ApplyModeVisibility();

        AppendLog(L"就绪。请选择传输模式 (局域网直连 / 房间码中继)。\r\n");
        AppendLog(L"提示: 房间码中继模式需先在公网 VPS 上运行 FileTransferRelay.exe\r\n");
        return 0;
    }

    case WM_SIZE:
        DoLayout(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_SYSCOMMAND:
        // 最小化时隐藏到托盘而非任务栏
        if (wParam == SC_MINIMIZE) {
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    case WM_TRAYICON:
        switch (lParam) {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            break;
        case WM_RBUTTONUP:
            ShowTrayMenu(hWnd);
            break;
        }
        return 0;

    case WM_DPICHANGED: {
        // DPI 变化时调整窗口大小并重新布局
        auto* rc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, rc->left, rc->top,
                     rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RECT clientRc;
        GetClientRect(hWnd, &clientRc);
        DoLayout(clientRc.right, clientRc.bottom);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = MIN_W;
        mmi->ptMinTrackSize.y = MIN_H;
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case IDC_MODE_LAN:
            g_ctx.mode = TransferMode::LAN;
            ApplyModeVisibility();
            AppendLog(L"[模式] 切换到局域网直连\r\n");
            break;
        case IDC_MODE_RELAY:
            g_ctx.mode = TransferMode::RELAY;
            ApplyModeVisibility();
            AppendLog(L"[模式] 切换到房间码中继 (跨局域网)\r\n");
            break;
        case IDC_FILE_BROWSE: BrowseFile(g_ctx.hFileEdit); break;
        case IDC_DIR_BROWSE: BrowseFolder(g_ctx.hDirEdit); break;
        case IDC_RSEND_FILE_BROWSE: BrowseFile(g_ctx.hRSendFileEdit); break;
        case IDC_RRECV_DIR_BROWSE: BrowseFolder(g_ctx.hRRecvDirEdit); break;
        case IDC_RSEND_ADV_BTN: {
            if (ShowAdvRelayDialog(hWnd)) {
                if (g_ctx.use_custom_relay) {
                    AppendLog(L"[高级设置] 已切换到自定义中继服务器: " +
                              utf8_to_wide(g_ctx.custom_relay_host) + L":" +
                              std::to_wstring(g_ctx.custom_relay_port) + L"\r\n");
                } else {
                    AppendLog(L"[高级设置] 已恢复使用默认中继服务器\r\n");
                }
            }
            break;
        }
        case IDC_SEND_BTN: {
            if (g_ctx.busy.load()) break;
            std::wstring port_w = GetTextW(g_ctx.hSendPortEdit);
            std::wstring file_w = GetTextW(g_ctx.hFileEdit);
            bool port_ok; unsigned short port = ParsePort(port_w, port_ok);
            if (!port_ok) {
                MessageBoxW(hWnd, L"端口号必须是 1-65535 的数字", L"提示", MB_OK | MB_ICONWARNING); break;
            }
            if (file_w.empty()) {
                MessageBoxW(hWnd, L"请选择要发送的文件", L"提示", MB_OK | MB_ICONWARNING); break;
            }
            g_ctx.busy = true; g_ctx.cancel = false;
            SetTransferControls(FALSE);
            SendMessageW(g_ctx.hProgress, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_ctx.hLog, L"");
            AppendLog(L"========== 发送文件 (局域网自动发现) ==========\r\n");
            // ip 传空字符串, 工作线程会自动发现
            g_ctx.worker = std::thread(TransferThread_LAN, true,
                                       std::string(), port, wide_to_utf8(file_w));
            break;
        }
        case IDC_RECV_BTN: {
            if (g_ctx.busy.load()) break;
            std::wstring port_w = GetTextW(g_ctx.hRecvPortEdit);
            std::wstring dir_w = GetTextW(g_ctx.hDirEdit);
            bool port_ok; unsigned short port = ParsePort(port_w, port_ok);
            if (!port_ok) {
                MessageBoxW(hWnd, L"端口号必须是 1-65535 的数字", L"提示", MB_OK | MB_ICONWARNING); break;
            }
            g_ctx.busy = true; g_ctx.cancel = false;
            SetTransferControls(FALSE);
            SendMessageW(g_ctx.hProgress, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_ctx.hLog, L"");
            AppendLog(L"========== 接收文件 (局域网直连) ==========\r\n");
            // 显示本机所有可用 IP
            auto ips = ft::get_local_ipv4_addresses();
            if (ips.empty()) {
                AppendLog(L"警告: 未能检测到本机 IPv4 地址\r\n");
            } else {
                AppendLog(L"本机 IP 地址:\r\n");
                for (const auto& ip : ips) {
                    std::wstring w = utf8_to_wide(ip) + L":" + std::to_wstring(port) + L"\r\n";
                    AppendLog(w.c_str());
                }
            }
            // 启动 UDP 发现响应线程, 让发送端能自动发现本机
            g_ctx.discovery_running = true;
            g_ctx.discovery_worker = ft::start_discovery_responder(port, g_ctx.discovery_running);
            AppendLog(L"已开启局域网自动发现, 等待发送端连接...\r\n");
            g_ctx.worker = std::thread(TransferThread_LAN, false,
                                       std::string(), port, wide_to_utf8(dir_w));
            break;
        }
        case IDC_RSEND_BTN: {
            if (g_ctx.busy.load()) break;
            std::wstring file_w = GetTextW(g_ctx.hRSendFileEdit);
            std::string host; unsigned short port;
            if (!get_effective_relay_addr(host, port)) {
                MessageBoxW(hWnd, L"中继服务器地址解析失败", L"错误", MB_OK | MB_ICONERROR); break;
            }
            if (file_w.empty()) {
                MessageBoxW(hWnd, L"请选择要发送的文件", L"提示", MB_OK | MB_ICONWARNING); break;
            }
            g_ctx.busy = true; g_ctx.cancel = false;
            SetTransferControls(FALSE);
            SendMessageW(g_ctx.hProgress, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_ctx.hLog, L"");
            SetWindowTextW(g_ctx.hRSendCodeEdit, L"创建中...");
            AppendLog(L"========== 中继发送 (创建房间) ==========\r\n");
            g_ctx.worker = std::thread(TransferThread_RelaySend,
                                       host, port, wide_to_utf8(file_w));
            break;
        }
        case IDC_RRECV_BTN: {
            if (g_ctx.busy.load()) break;
            std::wstring code_w = GetTextW(g_ctx.hRRecvCodeEdit);
            std::wstring dir_w = GetTextW(g_ctx.hRRecvDirEdit);
            std::string host; unsigned short port;
            if (!get_effective_relay_addr(host, port)) {
                MessageBoxW(hWnd, L"中继服务器地址解析失败", L"错误", MB_OK | MB_ICONERROR); break;
            }
            // 校验房间码: 6 位字母数字 (大小写不敏感)
            std::wstring code_trim = code_w;
            while (!code_trim.empty() && (code_trim.front() == L' ' || code_trim.front() == L'\t'))
                code_trim.erase(code_trim.begin());
            while (!code_trim.empty() && (code_trim.back() == L' ' || code_trim.back() == L'\t'))
                code_trim.pop_back();
            bool code_ok = (code_trim.size() == ft::ROOM_CODE_LEN);
            if (code_ok) {
                for (wchar_t c : code_trim) {
                    if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                          (c >= L'0' && c <= L'9'))) { code_ok = false; break; }
                }
            }
            if (!code_ok) {
                MessageBoxW(hWnd, L"房间码必须是 6 位字母数字", L"提示", MB_OK | MB_ICONWARNING); break;
            }
            g_ctx.busy = true; g_ctx.cancel = false;
            SetTransferControls(FALSE);
            SendMessageW(g_ctx.hProgress, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_ctx.hLog, L"");
            AppendLog(L"========== 中继接收 (加入房间) ==========\r\n");
            g_ctx.worker = std::thread(TransferThread_RelayRecv,
                                       host, port, wide_to_utf8(code_trim), wide_to_utf8(dir_w));
            break;
        }
        case IDC_CANCEL_BTN:
            if (g_ctx.busy.load()) {
                g_ctx.cancel = true;
                AppendLog(L"[取消] 正在终止传输...\r\n");
                EnableWindow(g_ctx.hCancelBtn, FALSE);
            }
            break;
        }
        return 0;
    }

    case WM_APP_UPDATE: {
        auto* pm = reinterpret_cast<ProgressMsg*>(lParam);
        if (pm) {
            if (pm->total > 0) {
                int pct = (int)(100 * pm->done / pm->total);
                SendMessageW(g_ctx.hProgress, PBM_SETPOS, pct, 0);
            }
            if (!pm->text.empty()) AppendLog(utf8_to_wide(pm->text) + L"\r\n");
            delete pm;
        }
        return 0;
    }

    case WM_APP_ROOM_CODE: {
        // 收到房间码, 显示到发送方房间码框
        auto* code = reinterpret_cast<std::string*>(lParam);
        if (code) {
            std::wstring wcode = utf8_to_wide(*code);
            SetWindowTextW(g_ctx.hRSendCodeEdit, wcode.c_str());
            AppendLog(L"[房间码] " + wcode + L"  (将此房间码告知接收方)\r\n");
            delete code;
        }
        return 0;
    }

    case WM_APP_DONE: {
        if (g_ctx.worker.joinable()) g_ctx.worker.join();
        // 停止接收端的 UDP 发现响应线程
        if (g_ctx.discovery_worker.joinable()) {
            g_ctx.discovery_running = false;
            g_ctx.discovery_worker.join();
        }
        g_ctx.busy = false;
        SetTransferControls(TRUE);
        int ret = (int)wParam;
        if (ret == 0) {
            SendMessageW(g_ctx.hProgress, PBM_SETPOS, 100, 0);
            AppendLog(L"\r\n[完成] 传输成功!\r\n");
            MessageBoxW(hWnd, L"传输完成!", L"成功", MB_OK | MB_ICONINFORMATION);
        } else if (ret == ft::CANCELED) {
            AppendLog(L"\r\n[已取消] 传输已终止\r\n");
        } else {
            AppendLog(L"\r\n[失败] 传输出错, 错误码: " + std::to_wstring(ret) + L"\r\n");
            MessageBoxW(hWnd, L"传输失败, 请查看日志了解详情",
                        L"失败", MB_OK | MB_ICONERROR);
        }
        return 0;
    }

    case WM_CLOSE: {
        // 1. 判断是否要走"退出"流程
        bool should_quit = g_ctx.force_quit;
        g_ctx.force_quit = false;

        if (!should_quit) {
            // 正常点击关闭按钮 — 根据记住的选择或弹对话框
            if (g_ctx.close_action == 1) {
                // 记住了"最小化到托盘"
                ShowWindow(hWnd, SW_HIDE);
                return 0;
            } else if (g_ctx.close_action == 0) {
                // 需要询问
                int choice = ShowCloseChoiceDialog(hWnd);
                if (choice == 1) {
                    ShowWindow(hWnd, SW_HIDE);
                    return 0;       // 最小化, 不关闭
                } else if (choice == 0) {
                    return 0;       // 取消, 不关闭
                }
                // choice == 2: 退出, 继续往下
            }
            // close_action == 2: 直接退出
        }

        // 2. 传输中确认
        if (g_ctx.busy.load()) {
            if (MessageBoxW(hWnd, L"传输正在进行中, 确定要退出吗?\n(将取消当前传输)",
                    L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                g_ctx.force_quit = false;
                return 0;
            }
            g_ctx.cancel = true;
            // 给 worker 线程最多 2 秒时间优雅退出
            for (int i = 0; i < 20 && g_ctx.busy.load(); ++i) {
                Sleep(100);
            }
            if (g_ctx.worker.joinable()) {
                if (g_ctx.busy.load()) g_ctx.worker.detach();
                else g_ctx.worker.join();
            }
        }
        // 3. 停止发现响应线程
        if (g_ctx.discovery_worker.joinable()) {
            g_ctx.discovery_running = false;
            g_ctx.discovery_worker.join();
        }
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY:
        TrayDelete();
        if (g_ctx.hFont) DeleteObject(g_ctx.hFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ========== 入口 ==========
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // 单实例检测: 防止同一台电脑运行多个客户端
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"FileTransfer_Client_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        HWND existing = FindWindowW(L"FileTransferMainWindow", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    // 启用 DPI 感知, 避免高 DPI 屏幕上控件错位/遮挡
    SetProcessDPIAware();

    // 保存实例句柄 + 加载关闭行为偏好
    g_ctx.hInst = hInstance;
    g_ctx.close_action = LoadCloseAction();
    LoadCustomRelay();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!ft::init_network()) {
        MessageBoxW(nullptr, L"网络初始化失败", L"错误", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    const wchar_t* cls_name = L"FileTransferMainWindow";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls_name;
    // 从 EXE 内嵌资源加载图标 (资源 ID = 1, 对应 app.rc 中的 1 ICON "...")
    // LoadIcon 会自动从多尺寸 ICO 中选择合适的分辨率
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    // 计算窗口大小 (客户区 760x860, 可调整)
    RECT rc = {0, 0, 760, 860};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

    g_ctx.hwnd = CreateWindowExW(
        0, cls_name, L"FileTransfer v0.0.3 - 文件传输",
        WS_OVERLAPPEDWINDOW,  // 完整窗口样式: 可调整大小、可最大化
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_ctx.hwnd, nCmdShow);
    UpdateWindow(g_ctx.hwnd);

    // 显式设置窗口图标 (标题栏小图标 + 任务栏大图标), 覆盖默认窗口类图标
    auto loadIcon = [&](int cx, int cy) -> HICON {
        return (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 cx, cy, LR_LOADTRANSPARENT);
    };
    HICON hIconSmall = loadIcon(GetSystemMetrics(SM_CXSMICON),
                               GetSystemMetrics(SM_CYSMICON));
    HICON hIconBig   = loadIcon(GetSystemMetrics(SM_CXICON),
                               GetSystemMetrics(SM_CYICON));
    if (hIconSmall) SendMessageW(g_ctx.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    if (hIconBig)   SendMessageW(g_ctx.hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);

    // 创建系统托盘图标
    TrayCreate();

    // 注册 TaskbarCreated 消息 (Explorer 重启后自动恢复托盘图标)
    UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Explorer 重启后重建托盘图标
        if (msg.message == WM_TASKBARCREATED && g_ctx.tray_created) {
            g_ctx.tray_created = false;
            TrayCreate();
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND focus = GetFocus();
            int id = GetDlgCtrlID(focus);
            // 各模式下回车触发对应主按钮
            if (g_ctx.mode == TransferMode::LAN) {
                if (id == IDC_SEND_PORT_EDIT || id == IDC_FILE_EDIT) {
                    if (IsWindowEnabled(g_ctx.hSendBtn))
                        PostMessageW(g_ctx.hwnd, WM_COMMAND, IDC_SEND_BTN, 0);
                    continue;
                }
                if (id == IDC_RECV_PORT_EDIT || id == IDC_DIR_EDIT) {
                    if (IsWindowEnabled(g_ctx.hRecvBtn))
                        PostMessageW(g_ctx.hwnd, WM_COMMAND, IDC_RECV_BTN, 0);
                    continue;
                }
            } else {
                if (id == IDC_RSEND_FILE_EDIT) {
                    if (IsWindowEnabled(g_ctx.hRSendBtn))
                        PostMessageW(g_ctx.hwnd, WM_COMMAND, IDC_RSEND_BTN, 0);
                    continue;
                }
                if (id == IDC_RRECV_CODE_EDIT || id == IDC_RRECV_DIR_EDIT) {
                    if (IsWindowEnabled(g_ctx.hRRecvBtn))
                        PostMessageW(g_ctx.hwnd, WM_COMMAND, IDC_RRECV_BTN, 0);
                    continue;
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ft::cleanup_network();
    CoUninitialize();
    return (int)msg.wParam;
}
