// Silex GUI 主程序 - Win32 原生窗口界面
// 支持两种模式: 局域网直连 / 房间码中继 (跨局域网, 含二维码扫码)
#include "file_transfer.h"
#include "relay.h"
#include "secret.h"
#include "qrcodegen.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <dpapi.h>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "dwmapi.lib")

// 启用 XP+ 视觉样式
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' "\
    "processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' "\
    "language='*'\"")

// ========== 布局常量 (96 DPI 基准, 运行时按 DPI 缩放) ==========
static const int MARGIN = 20;          // 外边距
static const int LABEL_W = 72;         // 标签列宽度 (卡片内表单标签)
static const int EDIT_H = 44;          // 输入框高度
static const int BTN_H = 48;           // 按钮高度
static const int BROWSE_W = 96;        // 浏览按钮宽度
static const int CARD_RADIUS = 12;     // 卡片圆角半径
static const int CARD_PADDING = 24;    // 卡片内边距
static const int TAB_H = 52;           // 模式标签栏高度
static const int TITLE_BAR_H = 56;     // 标题栏高度
static const int MAX_CONTENT_W = 1100; // 内容区最大宽度 (超出则居中)
// 最小窗口客户区尺寸
static const int MIN_W = 860;
static const int MIN_H = 800;

// DPI 缩放辅助
static float g_dpiScale = 1.0f;
static int Dpi(int v) { return (int)(v * g_dpiScale + 0.5f); }

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
    // 专用消息: 中继发送方收到房间码后, 更新二维码显示
    WM_APP_QR_UPDATE,
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

    HWND hLog = nullptr, hCancelBtn = nullptr, hLogLbl = nullptr;

    // 系统托盘 + 关闭行为偏好
    HINSTANCE hInst = nullptr;
    NOTIFYICONDATAW nid = {};
    bool tray_created = false;
    bool force_quit = false;       // 托盘"退出"触发, 跳过关闭对话框
    int close_action = 0;          // 0=询问, 1=最小化到托盘, 2=退出

    // ===== 中继模式二维码显示 (在中继发送区域旁边) =====
    HWND hQrImage = nullptr;       // 内嵌二维码显示控件 (STATIC + SS_BITMAP)
    HWND hQrCodeLbl2 = nullptr;    // 二维码下方的提示文字
    std::string qr_data;           // 二维码内容数据
    HBITMAP qr_bitmap = nullptr;   // 二维码位图
    int qr_bitmap_size = 0;        // 二维码位图边长 (像素)
    unsigned short qr_listen_port = 0;  // 预留 (未使用)

    // ===== 卡片式 UI 绘制相关 =====
    // 卡片区域矩形 (用于 WM_PAINT 绘制)
    RECT rcCard1 = {};     // 发送卡片
    RECT rcCard2 = {};     // 接收卡片
    RECT rcProgress = {};  // 进度条卡片
    RECT rcLog = {};       // 日志卡片
    RECT rcTabBar = {};    // 模式标签栏

    // 自定义窗口按钮 (交通灯)
    RECT rcBtnMin = {};    // 最小化按钮
    RECT rcBtnMax = {};    // 最大化/还原按钮
    RECT rcBtnCls = {};    // 关闭按钮
    bool hoverMin=false, hoverMax=false, hoverCls=false;
    RECT rcRestore = {};   // 最大化前的窗口位置
    bool maximized = false; // 自定义最大化标志 (IsZoomed 对 WS_POPUP 不可靠)

    // 按钮悬停状态
    bool hoverSend=false, hoverRecv=false, hoverRSend=false, hoverRRecv=false;
    bool hoverCancel=false, hoverBrowse1=false, hoverBrowse2=false;
    bool hoverBrowse3=false, hoverBrowse4=false;
    bool hoverAdv=false, hoverTabLan=false, hoverTabRelay=false;
    // 按钮按下状态
    bool pressSend=false, pressRecv=false, pressRSend=false, pressRRecv=false;
    bool pressCancel=false, pressBrowse1=false, pressBrowse2=false;
    bool pressBrowse3=false, pressBrowse4=false, pressAdv=false;

    // 额外字体
    HFONT hFontTitle = nullptr;   // 标题字体 (16px, bold)
    HFONT hFontCard = nullptr;    // 卡片标题字体 (14px, bold)
    HFONT hFontSmall = nullptr;   // 描述文字字体 (11px)
    HFONT hFontBtn = nullptr;     // 按钮字体 (13px)
    HFONT hFontTab = nullptr;     // 模式标签字体 (更大)

    // 自绘进度条状态
    uint64_t prog_done = 0;
    uint64_t prog_total = 0;
    int prog_pct = 0;
    bool prog_active = false;
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

// ========== 输入框: 文字水平 + 垂直完全居中 ==========
// Win32 单行 edit 无法让文字垂直居中 (EM_SETRECT 只对多行控件生效),
// 因此输入框统一创建为 ES_MULTILINE + ES_AUTOHSCROLL (行为等同单行, 不换行),
// 再用 EM_SETRECT 把格式化矩形收窄为一行高度并垂直居中。
// 多行 edit 不支持 EM_SETCUEBANNER, 空状态提示文字由子类化自行居中绘制。

// 前向声明 (EditCenteredProc 中需要在 WM_SIZE 时重新计算垂直居中)
static void CenterEditTextVertically(HWND hEdit);

static LRESULT CALLBACK EditCenteredProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    switch (msg) {
    case WM_PAINT: {
        // 每次重绘前确保格式矩形仍为垂直居中 (caret 位置跟随, 兜底防重置)
        CenterEditTextVertically(hwnd);
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int rad = Dpi(16); // 圆角直径
        // 先铺满卡片白色, 圆角之外的角落与卡片背景融合, 避免露出方形角
        HBRUSH white = CreateSolidBrush(RGB(255,255,255));
        FillRect(hdc, &rc, white);
        DeleteObject(white);
        // 圆角内填充浅灰背景
        HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, rad, rad);
        HBRUSH bg = CreateSolidBrush(RGB(248,250,252));
        FillRgn(hdc, rgn, bg);
        // 圆角边框 (聚焦时高亮)
        bool focused = (GetFocus() == hwnd);
        HPEN pen = CreatePen(PS_SOLID, 1, focused ? RGB(120,130,255) : RGB(224,230,240));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, 1, 1, rc.right - 2, rc.bottom - 2, rad, rad);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBr);
        DeleteObject(pen);
        DeleteObject(rgn);
        DeleteObject(bg);
        // 文字: 空 + 无焦点显示灰色提示; 有内容显示深色文字, 均完全居中
        const wchar_t* cue = reinterpret_cast<const wchar_t*>(dwRefData);
        bool empty = GetWindowTextLengthW(hwnd) == 0;
        HFONT f = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        HFONT oldF = f ? (HFONT)SelectObject(hdc, f) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        if (empty && !focused && cue && cue[0]) {
            SetTextColor(hdc, RGB(148,163,184));
            DrawTextW(hdc, cue, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (!empty) {
            std::wstring txt = GetTextW(hwnd);
            SetTextColor(hdc, IsWindowEnabled(hwnd) ? RGB(51,65,85) : RGB(170,180,195));
            SIZE sz = {};
            GetTextExtentPoint32W(hdc, txt.c_str(), (int)txt.size(), &sz);
            if (sz.cx <= (rc.right - rc.left)) {
                DrawTextW(hdc, txt.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                // 超宽文本从左侧开始显示 (尾部裁剪)
                RECT lrc = rc;
                InflateRect(&lrc, -Dpi(4), 0);
                DrawTextW(hdc, txt.c_str(), -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        if (oldF) SelectObject(hdc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        // 背景由 WM_PAINT 自绘, 防止闪烁
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        // 焦点变化时刷新提示文字与边框高亮
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    }
    LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
    // 控件尺寸变化 (MoveWindow/DoLayout) 或文本被设置 (SetWindowTextW)
    // 都会重置多行编辑框的格式矩形, 需重新计算垂直居中
    if (msg == WM_SIZE || msg == WM_SETTEXT) CenterEditTextVertically(hwnd);
    return r;
}

// 让输入框文字垂直居中: 将格式化矩形收窄为一行高度并垂直居中
static void CenterEditTextVertically(HWND hEdit) {
    if (!hEdit) return;
    RECT rc;
    GetClientRect(hEdit, &rc);
    if (rc.bottom <= 0) return;
    HDC hdc = GetDC(hEdit);
    HFONT f = (HFONT)SendMessageW(hEdit, WM_GETFONT, 0, 0);
    HFONT oldF = f ? (HFONT)SelectObject(hdc, f) : nullptr;
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    if (oldF) SelectObject(hdc, oldF);
    ReleaseDC(hEdit, hdc);
    int lineH = tm.tmHeight + tm.tmExternalLeading;
    if (lineH <= 0) lineH = rc.bottom;
    int dy = (rc.bottom - lineH) / 2;
    if (dy <= 0) return;
    RECT textRc = {rc.left, dy, rc.right, rc.bottom - dy};
    // 已居中则跳过, 避免每次重绘重复 EM_SETRECT (会触发重绘, 可能死循环)
    RECT cur = {};
    SendMessageW(hEdit, EM_GETRECT, 0, (LPARAM)&cur);
    if (cur.left == textRc.left && cur.top == textRc.top &&
        cur.right == textRc.right && cur.bottom == textRc.bottom) return;
    SendMessageW(hEdit, EM_SETRECT, 0, (LPARAM)&textRc);
}

// 创建文字完全居中的输入框 (placeholder 为空则不显示提示文字)
static HWND CreateEditCentered(HWND parent, const wchar_t* placeholder,
                               const wchar_t* text, DWORD extraStyle, int id) {
    HWND hEdit = CreateCtrl(parent, L"edit", text,
                   ES_MULTILINE | ES_AUTOHSCROLL | ES_CENTER | extraStyle,
                   0, 0, 10, 10, id);
    if (hEdit) SetWindowSubclass(hEdit, EditCenteredProc, 1, (DWORD_PTR)placeholder);
    return hEdit;
}

// 显示/隐藏一组控件 (用于模式切换)
static void ShowGroup(const std::initializer_list<HWND>& ctrls, BOOL show) {
    for (HWND h : ctrls) {
        if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    }
}

// ========== 布局: 根据客户区大小重排所有控件 (卡片式, DPI 缩放, 居中) ==========
static void DoLayout(int cx, int cy) {
    // DPI 缩放后的局部尺寸
    int margin   = Dpi(MARGIN);
    int labelW   = Dpi(LABEL_W);
    int editH    = Dpi(EDIT_H);
    int btnH     = Dpi(BTN_H);
    int browseW  = Dpi(BROWSE_W);
    int cardPad  = Dpi(CARD_PADDING);
    int tabH     = Dpi(TAB_H);
    int titleH   = Dpi(TITLE_BAR_H);

    // 内容区宽度 (限制最大宽度并居中, 避免全屏时内容拉散)
    int w = cx - margin * 2;
    int maxW = Dpi(MAX_CONTENT_W);
    if (w > maxW) w = maxW;
    if (w < 100) w = 100;
    int x = (cx - w) / 2;  // 居中

    bool lan = (g_ctx.mode == TransferMode::LAN);

    // ===== 模式标签栏 =====
    int tabGap = Dpi(8);
    int tabY = titleH + tabGap;
    g_ctx.rcTabBar = {x, tabY, x + w, tabY + tabH};
    int tabHalfW = w / 2;
    MoveWindow(g_ctx.hModeLan, x + 3, tabY + 3, tabHalfW - 3, tabH - 6, TRUE);
    MoveWindow(g_ctx.hModeRelay, x + tabHalfW, tabY + 3, w - tabHalfW - 3, tabH - 6, TRUE);

    // ===== 卡片1 (发送区) - 高度由内容计算 =====
    int headerH = Dpi(30);
    int gap1 = Dpi(10);
    int gap2 = Dpi(8);
    int gap3 = Dpi(10);
    int c1y = tabY + tabH + tabGap;
    int contentX = x + cardPad + labelW;
    int innerW = w - cardPad * 2 - labelW;

    int qrSize = Dpi(90);
    int c1H = lan
        ? (cardPad + headerH + gap1 + editH + gap2 + editH + gap3 + btnH + cardPad)
        : (cardPad + headerH + gap1 + editH + gap2 + editH + gap3 + btnH + gap2 + qrSize + cardPad);
    g_ctx.rcCard1 = {x, c1y, x + w, c1y + c1H};

    int r1y = c1y + cardPad + headerH + gap1;
    int r2y = r1y + editH + gap2;
    int r3y = r2y + editH + gap3;

    if (lan) {
        MoveWindow(g_ctx.hSendPortLbl, x + cardPad, r1y + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        MoveWindow(g_ctx.hSendPortEdit, contentX, r1y, Dpi(72), editH, TRUE);
        MoveWindow(g_ctx.hFileLbl, x + cardPad, r2y + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        int fileEditW = innerW - browseW - 6;
        MoveWindow(g_ctx.hFileEdit, contentX, r2y, fileEditW, editH, TRUE);
        MoveWindow(g_ctx.hFileBrowse, contentX + fileEditW + 6, r2y, browseW, editH, TRUE);
        MoveWindow(g_ctx.hSendBtn, contentX, r3y, Dpi(130), btnH, TRUE);
    } else {
        MoveWindow(g_ctx.hRSendFileLbl, x + cardPad, r1y + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        int rFileEditW = innerW - browseW - 6;
        MoveWindow(g_ctx.hRSendFileEdit, contentX, r1y, rFileEditW, editH, TRUE);
        MoveWindow(g_ctx.hRSendFileBrowse, contentX + rFileEditW + 6, r1y, browseW, editH, TRUE);
        // 房间码行
        MoveWindow(g_ctx.hRSendCodeLbl, x + cardPad, r2y + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        MoveWindow(g_ctx.hRSendCodeEdit, contentX, r2y, Dpi(160), editH, TRUE);
        // 按钮行 (创建房间 + 高级设置)
        MoveWindow(g_ctx.hRSendBtn, contentX, r3y, Dpi(160), btnH, TRUE);
        MoveWindow(g_ctx.hRSendAdvBtn, contentX + Dpi(170), r3y, Dpi(90), btnH, TRUE);
        // QR 码区域 (按钮行下方)
        int qrY = r3y + btnH + gap2;
        MoveWindow(g_ctx.hQrImage, contentX, qrY, qrSize, qrSize, TRUE);
        int qrLabelX = contentX + qrSize + Dpi(12);
        int qrLabelW = w - cardPad - (qrLabelX - x);
        MoveWindow(g_ctx.hQrCodeLbl2, qrLabelX, qrY + Dpi(10), qrLabelW, Dpi(48), TRUE);
    }

    // ===== 卡片2 (接收区) - 高度由内容计算 =====
    int c2Gap = Dpi(8);
    int c2y = c1y + c1H + c2Gap;
    int c2H = cardPad + headerH + gap1 + editH + gap2 + editH + gap2 + btnH + cardPad;
    g_ctx.rcCard2 = {x, c2y, x + w, c2y + c2H};

    int r1y2 = c2y + cardPad + headerH + gap1;
    int r2y2 = r1y2 + editH + gap2;
    int r3y2 = r2y2 + editH + gap2;

    if (lan) {
        MoveWindow(g_ctx.hRecvPortLbl, x + cardPad, r1y2 + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        MoveWindow(g_ctx.hRecvPortEdit, contentX, r1y2, Dpi(72), editH, TRUE);
        MoveWindow(g_ctx.hDirLbl, x + cardPad, r2y2 + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        int dirEditW = innerW - browseW - 6;
        MoveWindow(g_ctx.hDirEdit, contentX, r2y2, dirEditW, editH, TRUE);
        MoveWindow(g_ctx.hDirBrowse, contentX + dirEditW + 6, r2y2, browseW, editH, TRUE);
        MoveWindow(g_ctx.hRecvBtn, contentX, r3y2, Dpi(140), btnH, TRUE);
    } else {
        MoveWindow(g_ctx.hRRecvCodeLbl, x + cardPad, r1y2 + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        MoveWindow(g_ctx.hRRecvCodeEdit, contentX, r1y2, Dpi(160), editH, TRUE);
        MoveWindow(g_ctx.hRRecvDirLbl, x + cardPad, r2y2 + (editH - Dpi(28)) / 2, labelW, Dpi(28), TRUE);
        int rDirEditW = innerW - browseW - 6;
        MoveWindow(g_ctx.hRRecvDirEdit, contentX, r2y2, rDirEditW, editH, TRUE);
        MoveWindow(g_ctx.hRRecvDirBrowse, contentX + rDirEditW + 6, r2y2, browseW, editH, TRUE);
        MoveWindow(g_ctx.hRRecvBtn, contentX, r3y2, Dpi(180), btnH, TRUE);
    }

    // ===== 进度条卡片 =====
    int pGap = Dpi(8);
    int pY = c2y + c2H + pGap;
    int pH = Dpi(56);
    g_ctx.rcProgress = {x, pY, x + w, pY + pH};

    // 取消按钮右对齐 (进度条与进度文本由 WM_PAINT 自绘)
    int cancelW = Dpi(88);
    int cancelH = Dpi(34);
    MoveWindow(g_ctx.hCancelBtn, x + w - cardPad - cancelW, pY + (pH - cancelH) / 2,
               cancelW, cancelH, TRUE);

    // ===== 日志卡片 (自适应填充剩余空间) =====
    int lGap = Dpi(8);
    int lY = pY + pH + lGap;
    int lH = cy - lY - margin;
    if (lH < Dpi(80)) lH = Dpi(80);
    g_ctx.rcLog = {x, lY, x + w, lY + lH};

    if (g_ctx.hLogLbl) ShowWindow(g_ctx.hLogLbl, SW_HIDE);
    int logContentY = lY + Dpi(36);
    int logContentH = lY + lH - logContentY - cardPad;
    if (logContentH < Dpi(50)) logContentH = Dpi(50);
    MoveWindow(g_ctx.hLog, x + cardPad, logContentY, w - cardPad * 2, logContentH, TRUE);

    // ===== 交通灯按钮 (标题栏右侧) =====
    int btnSize = Dpi(12);
    int btnGap = Dpi(6);
    int btnY = (titleH - btnSize) / 2;
    int rightEdge = cx - margin;
    g_ctx.rcBtnCls = { rightEdge - btnSize, btnY, rightEdge, btnY + btnSize };
    g_ctx.rcBtnMax = { rightEdge - btnSize*2 - btnGap, btnY, rightEdge - btnSize - btnGap, btnY + btnSize };
    g_ctx.rcBtnMin = { rightEdge - btnSize*3 - btnGap*2, btnY, rightEdge - btnSize*2 - btnGap*2, btnY + btnSize };
}

// ========== 模式切换: 显示/隐藏对应控件 ==========
static void ApplyModeVisibility() {
    bool lan = (g_ctx.mode == TransferMode::LAN);
    bool relay = (g_ctx.mode == TransferMode::RELAY);

    // 先隐藏所有控件, 确保旧控件不可见
    ShowGroup({
        g_ctx.hSendPortLbl, g_ctx.hSendPortEdit, g_ctx.hFileLbl, g_ctx.hFileEdit,
        g_ctx.hFileBrowse, g_ctx.hSendBtn,
        g_ctx.hRecvPortLbl, g_ctx.hRecvPortEdit,
        g_ctx.hDirLbl, g_ctx.hDirEdit, g_ctx.hDirBrowse, g_ctx.hRecvBtn,
        g_ctx.hRSendFileLbl, g_ctx.hRSendFileEdit,
        g_ctx.hRSendFileBrowse, g_ctx.hRSendBtn, g_ctx.hRSendAdvBtn,
        g_ctx.hRSendCodeLbl, g_ctx.hRSendCodeEdit,
        g_ctx.hRRecvCodeLbl, g_ctx.hRRecvCodeEdit,
        g_ctx.hRRecvDirLbl, g_ctx.hRRecvDirEdit, g_ctx.hRRecvDirBrowse, g_ctx.hRRecvBtn,
    }, FALSE);

    // 再显示需要的控件
    if (lan) {
        ShowGroup({
            g_ctx.hSendPortLbl, g_ctx.hSendPortEdit, g_ctx.hFileLbl, g_ctx.hFileEdit,
            g_ctx.hFileBrowse, g_ctx.hSendBtn,
            g_ctx.hRecvPortLbl, g_ctx.hRecvPortEdit,
            g_ctx.hDirLbl, g_ctx.hDirEdit, g_ctx.hDirBrowse, g_ctx.hRecvBtn,
        }, TRUE);
    }

    if (relay) {
        ShowGroup({
            g_ctx.hRSendFileLbl, g_ctx.hRSendFileEdit,
            g_ctx.hRSendFileBrowse, g_ctx.hRSendBtn, g_ctx.hRSendAdvBtn,
            g_ctx.hRSendCodeLbl, g_ctx.hRSendCodeEdit,
            g_ctx.hRRecvCodeLbl, g_ctx.hRRecvCodeEdit,
            g_ctx.hRRecvDirLbl, g_ctx.hRRecvDirEdit, g_ctx.hRRecvDirBrowse, g_ctx.hRRecvBtn,
        }, TRUE);
    }

    // 二维码显示
    ShowWindow(g_ctx.hQrImage, relay && !g_ctx.qr_data.empty() ? SW_SHOW : SW_HIDE);
    ShowWindow(g_ctx.hQrCodeLbl2, relay && !g_ctx.qr_data.empty() ? SW_SHOW : SW_HIDE);

    // 重新布局 + 同步刷新
    RECT rc;
    GetClientRect(g_ctx.hwnd, &rc);
    DoLayout(rc.right, rc.bottom);
    // 强制同步刷新 Tab 按钮 (使用 RedrawWindow 代替 InvalidateRect 立即生效)
    if (g_ctx.hModeLan) RedrawWindow(g_ctx.hModeLan, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    if (g_ctx.hModeRelay) RedrawWindow(g_ctx.hModeRelay, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    RedrawWindow(g_ctx.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
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

// 确保 Windows 防火墙入站规则 "Silex" 存在 (允许本程序接收入站 TCP 连接)
// wait=true 时等待 netsh 完成并复核规则是否生效; wait=false 时触发 UAC 后立即返回 (启动时非阻塞)
// 返回 true 表示规则确认已存在
static bool EnsureFirewallRule(bool wait) {
    const std::wstring check_cmd =
        L"netsh advfirewall firewall show rule name=\"Silex\" >nul 2>&1";
    if (_wsystem(check_cmd.c_str()) == 0) return true;  // 规则已存在

    // 规则缺失, 以管理员权限添加 (触发 UAC 提示)
    wchar_t exe_path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring params = L"advfirewall firewall add rule name=\"Silex\" "
        L"dir=in action=allow program=\"" + std::wstring(exe_path) + L"\" enable=yes";
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb = L"runas";
    sei.lpFile = L"netsh.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return false;  // UAC 被拒绝

    if (sei.hProcess) {
        if (wait) WaitForSingleObject(sei.hProcess, 15000);
        CloseHandle(sei.hProcess);
    }
    if (wait) return (_wsystem(check_cmd.c_str()) == 0);  // 复核规则
    return false;  // 非等待模式无法确认
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
        // 打印本机 IP, 便于排查广播是否已发出
        auto local_ips = ft::get_local_ipv4_addresses();
        std::string ipinfo = "[信息] 本机 IP: ";
        for (std::size_t i = 0; i < local_ips.size(); ++i)
            ipinfo += (i ? ", " : "") + local_ips[i];
        cb(0, 0, ipinfo);
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

// 二维码位图生成 (供 WM_APP_QR_UPDATE 在主线程调用)
// 二维码编码 FT1|R|relay_host|port|room_code, 手机扫码后自动加入房间接收
static HBITMAP GenerateQrBitmap(const std::string& text, int pixel_size, int& out_size);

// 中继发送 (同时生成房间码和二维码)
static void TransferThread_RelaySend(std::string host, unsigned short port, std::string path) {
    static const std::string kCodePrefix = "[房间码] ";
    // 值捕获 host/port, 用于回调中生成二维码
    auto cb = [host, port](uint64_t done, uint64_t total, const std::string& msg) -> bool {
        if (msg.rfind(kCodePrefix, 0) == 0) {
            std::string code = msg.substr(kCodePrefix.size());
            // 1. 显示房间码到房间码框
            PostMessageW(g_ctx.hwnd, WM_APP_ROOM_CODE, 0, (LPARAM)new std::string(code));
            // 2. 同时生成二维码 (FT1|R|relay_host|port|room_code), 显示在旁边空白位置
            std::string qr_data = "FT1|R|" + host + "|" + std::to_string(port) + "|" + code;
            PostMessageW(g_ctx.hwnd, WM_APP_QR_UPDATE, 0, (LPARAM)new std::string(qr_data));
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
static const wchar_t* REG_KEY = L"Software\\Silex";

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

// ========== 自定义中继服务器设置持久化 (注册表, DPAPI 加密) ==========
// 使用 Windows DPAPI 加密敏感数据, 防止注册表中明文暴露 IP 地址
// DPAPI 加密的数据仅当前用户 + 当前机器可解密, 复制到其他环境无效

// DPAPI 加密: 返回加密后的字节流
static std::vector<BYTE> DpapiEncrypt(const std::string& plaintext) {
    DATA_BLOB input = {};
    input.pbData = (BYTE*)plaintext.data();
    input.cbData = (DWORD)plaintext.size();
    DATA_BLOB output = {};
    if (!CryptProtectData(&input, L"Silex_Relay", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    std::vector<BYTE> result(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return result;
}

// DPAPI 解密: 返回解密后的字符串
static std::string DpapiDecrypt(const std::vector<BYTE>& ciphertext) {
    if (ciphertext.empty()) return {};
    DATA_BLOB input = {};
    input.pbData = (BYTE*)ciphertext.data();
    input.cbData = (DWORD)ciphertext.size();
    DATA_BLOB output = {};
    LPWSTR desc = nullptr;
    if (!CryptUnprotectData(&input, &desc, nullptr, nullptr,
                            nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    std::string result((char*)output.pbData, output.cbData);
    if (desc) LocalFree(desc);
    LocalFree(output.pbData);
    return result;
}

static void LoadCustomRelay() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD use_custom = 0, size = sizeof(use_custom);
        if (RegQueryValueExW(hKey, L"UseCustomRelay", nullptr, nullptr,
                             (LPBYTE)&use_custom, &size) == ERROR_SUCCESS && use_custom) {
            // 读取加密的 host (REG_BINARY, DPAPI 加密)
            DWORD host_type = 0;
            DWORD host_size = 0;
            if (RegQueryValueExW(hKey, L"CustomRelayHost", nullptr, &host_type,
                                 nullptr, &host_size) == ERROR_SUCCESS && host_size > 0) {
                std::vector<BYTE> host_buf(host_size);
                if (RegQueryValueExW(hKey, L"CustomRelayHost", nullptr, nullptr,
                                     host_buf.data(), &host_size) == ERROR_SUCCESS) {
                    std::string host;
                    if (host_type == REG_BINARY) {
                        // 新格式: DPAPI 加密
                        host = DpapiDecrypt(host_buf);
                    } else {
                        // 旧格式: REG_SZ 明文 (向后兼容, 加载后会在下次保存时自动迁移为加密格式)
                        std::wstring host_w((wchar_t*)host_buf.data(),
                                            host_size / sizeof(wchar_t));
                        // 去除可能的尾部 \0
                        while (!host_w.empty() && host_w.back() == L'\0') host_w.pop_back();
                        host = wide_to_utf8(host_w);
                    }
                    if (!host.empty()) {
                        // 读取端口 (DWORD, 端口号本身不敏感, 保持明文)
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
            // DPAPI 加密 host, 存为 REG_BINARY (防止注册表中明文暴露 IP)
            auto encrypted = DpapiEncrypt(g_ctx.custom_relay_host);
            if (!encrypted.empty()) {
                RegSetValueExW(hKey, L"CustomRelayHost", 0, REG_BINARY,
                               encrypted.data(), (DWORD)encrypted.size());
            }
            // 端口号本身不敏感, 保持 REG_DWORD
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
    wcscpy_s(g_ctx.nid.szTip, L"臻传 Silex - 文件传输");
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
        BeginPaint(hWnd, &ps);
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
        wc.lpszClassName = L"SilexCloseDlg";
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
        L"SilexCloseDlg",
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
    HWND hBtnOk = nullptr, hBtnClear = nullptr, hBtnCancel = nullptr;
    bool confirmed = false;
    bool use_custom = false;
    std::string ip;
    unsigned short port = 0;
    // 自绘样式状态
    RECT rcClose = {};           // 标题栏关闭按钮
    bool hoverClose = false, pressClose = false;
    bool hoverOk = false, hoverClear = false, hoverCancel = false;
};
static AdvRelayDlgState g_adv;

// 高级设置对话框窗口过程
// 前向声明 (定义在文件后面, 但对话框代码在前)
static void DrawGradientRoundRect(HDC hdc, const RECT& rc, int radius,
                                  COLORREF c1, COLORREF c2, bool vertical);
static void DrawSolidRoundRect(HDC hdc, const RECT& rc, int radius,
                               COLORREF fill, COLORREF border, int borderWidth);

// 高级设置对话框按钮绘制 (与主程序按钮风格一致)
static void DrawAdvButton(HDC hdc, const RECT& rc, const wchar_t* text,
                          bool primary, bool pressed, bool hover) {
    HFONT oldF = (HFONT)SelectObject(hdc, g_ctx.hFontBtn);
    SetBkMode(hdc, TRANSPARENT);
    if (primary) {
        // 主按钮: 蓝紫渐变
        COLORREF c1, c2;
        if (pressed) { c1 = RGB(91,101,192); c2 = RGB(109,120,218); }
        else if (hover) { c1 = RGB(117,127,222); c2 = RGB(149,160,255); }
        else { c1 = RGB(107,117,212); c2 = RGB(129,140,248); }
        DrawGradientRoundRect(hdc, rc, 8, c1, c2, false);
        SetTextColor(hdc, RGB(255,255,255));
    } else {
        // 次要按钮: 白底浅边框
        COLORREF fill, border, tc;
        if (pressed) { fill = RGB(238,242,250); border = RGB(165,180,252); tc = RGB(107,117,212); }
        else if (hover) { fill = RGB(245,248,255); border = RGB(165,180,252); tc = RGB(107,117,212); }
        else { fill = RGB(255,255,255); border = RGB(224,230,240); tc = RGB(100,116,139); }
        DrawSolidRoundRect(hdc, rc, 8, fill, border, 1);
        SetTextColor(hdc, tc);
    }
    RECT trc = rc;
    if (pressed) OffsetRect(&trc, 1, 1);
    DrawTextW(hdc, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);
}

static LRESULT CALLBACK AdvRelayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        auto d = Dpi;
        int x0 = d(32);
        int eW = d(400) - x0 * 2;
        int eH = d(44);
        int lblH = d(24);
        int btnH = d(40);
        int btnY = d(316);

        // 标签
        CreateWindowExW(0, L"STATIC", L"中继服务器 IP:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x0, d(76), d(120), lblH, hWnd, (HMENU)IDC_AR_IP_LABEL, g_ctx.hInst, nullptr);

        // IP 输入框 (自绘圆角, 文字居中, placeholder)
        g_adv.hIpEdit = CreateEditCentered(hWnd, L"输入自定义中继服务器 IP", L"", 0, IDC_AR_IP_EDIT);

        // 端口标签
        CreateWindowExW(0, L"STATIC", L"端口:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x0, d(160), d(120), lblH, hWnd, (HMENU)IDC_AR_PORT_LABEL, g_ctx.hInst, nullptr);

        // 端口输入框
        g_adv.hPortEdit = CreateEditCentered(hWnd, L"端口号", L"", ES_NUMBER, IDC_AR_PORT_EDIT);

        // 提示文字 (根据是否已设置自定义服务器显示不同状态, 不暴露具体 IP)
        std::wstring hint_text;
        if (g_ctx.use_custom_relay) {
            hint_text = L"当前状态: 已启用自定义中继服务器 (出于安全考虑不显示地址)\r\n"
                        L"点击\"恢复默认\"可切换回内置服务器, 或重新输入地址后点击\"确定\"。";
        } else {
            hint_text = L"当前状态: 使用内置中继服务器 (默认)\r\n"
                        L"如需切换到自定义服务器, 请输入地址和端口后点击\"确定\"。";
        }
        HWND hHint = CreateWindowExW(0, L"STATIC", hint_text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x0, d(244), eW, d(48), hWnd, (HMENU)IDC_AR_HINT, g_ctx.hInst, nullptr);

        // 按钮行 (右对齐: 确定 / 恢复默认 / 取消)
        g_adv.hBtnOk = CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_PUSHBUTTON,
            d(400) - x0 - d(88), btnY, d(88), btnH, hWnd, (HMENU)IDC_AR_OK, g_ctx.hInst, nullptr);
        g_adv.hBtnClear = CreateWindowExW(0, L"BUTTON", L"恢复默认",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_PUSHBUTTON,
            d(400) - x0 - d(88) - d(8) - d(96), btnY, d(96), btnH,
            hWnd, (HMENU)IDC_AR_CLEAR, g_ctx.hInst, nullptr);
        g_adv.hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_PUSHBUTTON,
            d(400) - x0 - d(88) - d(8) - d(96) - d(8) - d(72), btnY, d(72), btnH,
            hWnd, (HMENU)IDC_AR_CANCEL, g_ctx.hInst, nullptr);

        // 字体: 标签/提示用小号字体, 按钮用按钮字体
        SendMessageW(GetDlgItem(hWnd, IDC_AR_IP_LABEL), WM_SETFONT, (WPARAM)g_ctx.hFontSmall, TRUE);
        SendMessageW(GetDlgItem(hWnd, IDC_AR_PORT_LABEL), WM_SETFONT, (WPARAM)g_ctx.hFontSmall, TRUE);
        SendMessageW(hHint, WM_SETFONT, (WPARAM)g_ctx.hFontSmall, TRUE);
        SendMessageW(g_adv.hBtnOk, WM_SETFONT, (WPARAM)g_ctx.hFontBtn, TRUE);
        SendMessageW(g_adv.hBtnClear, WM_SETFONT, (WPARAM)g_ctx.hFontBtn, TRUE);
        SendMessageW(g_adv.hBtnCancel, WM_SETFONT, (WPARAM)g_ctx.hFontBtn, TRUE);

        // 关闭按钮矩形 (标题栏右上角, 红点风格, 与主程序一致)
        g_adv.rcClose = { d(400) - d(46), d(15), d(400) - d(24), d(36) };

        // 输入框位置
        MoveWindow(g_adv.hIpEdit, x0, d(104), eW, eH, TRUE);
        MoveWindow(g_adv.hPortEdit, x0, d(188), eW, eH, TRUE);
        return 0;
    }

    case WM_NCHITTEST: {
        // 标题栏可拖动, 关闭按钮区域自己处理点击
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINT cpt = pt;
        ScreenToClient(hWnd, &cpt);
        if (PtInRect(&g_adv.rcClose, cpt)) return HTCLIENT;
        if (cpt.y >= 0 && cpt.y <= Dpi(52)) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int titleH = Dpi(52);
        // 标题栏蓝紫渐变 (与主程序一致)
        TRIVERTEX tv[2] = {};
        tv[0].x = 0; tv[0].y = 0;
        tv[0].Red = 120 << 8; tv[0].Green = 130 << 8; tv[0].Blue = 220 << 8; tv[0].Alpha = 0;
        tv[1].x = rc.right; tv[1].y = titleH;
        tv[1].Red = 107 << 8; tv[1].Green = 117 << 8; tv[1].Blue = 212 << 8; tv[1].Alpha = 0;
        GRADIENT_RECT gr = {0, 1};
        GradientFill(hdc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_H);
        // 标题文字
        HFONT oldF = (HFONT)SelectObject(hdc, g_ctx.hFontCard);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255,255,255));
        RECT tRc = { Dpi(24), 0, rc.right - Dpi(56), titleH };
        DrawTextW(hdc, L"高级设置", -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldF);
        // 关闭按钮 (红点, 与主程序交通灯关闭键一致)
        HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
        HPEN oldPen = (HPEN)SelectObject(hdc, hNullPen);
        HBRUSH hClose = CreateSolidBrush(g_adv.hoverClose ? RGB(255,120,120) : RGB(255,107,107));
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hClose);
        Ellipse(hdc, g_adv.rcClose.left, g_adv.rcClose.top,
                g_adv.rcClose.right, g_adv.rcClose.bottom);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(hNullPen);
        DeleteObject(hClose);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hOver = ChildWindowFromPoint(hWnd, pt);
        bool nOk = (hOver == g_adv.hBtnOk);
        bool nClear = (hOver == g_adv.hBtnClear);
        bool nCancel = (hOver == g_adv.hBtnCancel);
        if (nOk != g_adv.hoverOk) { g_adv.hoverOk = nOk; InvalidateRect(g_adv.hBtnOk, nullptr, FALSE); }
        if (nClear != g_adv.hoverClear) { g_adv.hoverClear = nClear; InvalidateRect(g_adv.hBtnClear, nullptr, FALSE); }
        if (nCancel != g_adv.hoverCancel) { g_adv.hoverCancel = nCancel; InvalidateRect(g_adv.hBtnCancel, nullptr, FALSE); }
        bool nClose = PtInRect(&g_adv.rcClose, pt);
        if (nClose != g_adv.hoverClose) { g_adv.hoverClose = nClose; InvalidateRect(hWnd, &g_adv.rcClose, TRUE); }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&g_adv.rcClose, pt)) {
            g_adv.pressClose = true;
            InvalidateRect(hWnd, &g_adv.rcClose, TRUE);
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (g_adv.pressClose) {
            g_adv.pressClose = false;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&g_adv.rcClose, pt)) {
                g_adv.confirmed = false;
                DestroyWindow(hWnd);
                return 0;
            }
            InvalidateRect(hWnd, &g_adv.rcClose, TRUE);
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType != ODT_BUTTON) break;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool hover = (dis->CtlID == IDC_AR_OK) ? g_adv.hoverOk
                   : (dis->CtlID == IDC_AR_CLEAR) ? g_adv.hoverClear
                   : (dis->CtlID == IDC_AR_CANCEL) ? g_adv.hoverCancel : false;
        const wchar_t* text = (dis->CtlID == IDC_AR_OK) ? L"确定"
                            : (dis->CtlID == IDC_AR_CLEAR) ? L"恢复默认" : L"取消";
        DrawAdvButton(dis->hDC, dis->rcItem, text, dis->CtlID == IDC_AR_OK, pressed, hover);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        // 静态控件: 白底 + 灰色文字
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(100,116,139));
        static HBRUSH hWhite = CreateSolidBrush(RGB(255,255,255));
        return (INT_PTR)hWhite;
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
        wc.lpszClassName = L"SilexAdvRelayDlg";
        RegisterClassExW(&wc);
        registered = true;
    }

    int dlgW = Dpi(400), dlgH = Dpi(384);

    // 计算居中位置
    RECT parentRc;
    GetWindowRect(hParent, &parentRc);
    int x = parentRc.left + (parentRc.right - parentRc.left - dlgW) / 2;
    int y = parentRc.top + (parentRc.bottom - parentRc.top - dlgH) / 2;

    // 创建窗口 (无边框, 自绘标题栏, 与主程序风格一致)
    HWND hDlg = CreateWindowExW(0,
        L"SilexAdvRelayDlg",
        L"高级设置 - 自定义中继服务器",
        WS_POPUP,
        x, y, dlgW, dlgH,
        hParent, nullptr, g_ctx.hInst, nullptr);

    if (!hDlg) return false;
    g_adv.hwnd = hDlg;

    // 无边框窗口圆角 (与主程序一致)
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

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

// ========== 二维码生成与对话框 ==========

// 生成二维码位图 (HBITMAP)
// QR 内容格式: FT1|R|relay_host|port|room_code
static HBITMAP GenerateQrBitmap(const std::string& text, int pixel_size, int& out_size) {
    using namespace qrcodegen;
    QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::MEDIUM);
    int modules = qr.getSize();
    int border = 4;  // QR 规范要求 4 模块白边
    int total = (modules + border * 2) * pixel_size;
    out_size = total;

    // 创建 DIB section
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = total;
    bi.biHeight = -total;  // 自上而下
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hBmp) return nullptr;

    // 填充像素 (每行需要 4 字节对齐)
    int row_bytes = (total * 3 + 3) & ~3;
    auto* buf = (unsigned char*)bits;
    for (int y = 0; y < total; y++) {
        int module_y = y / pixel_size - border;
        for (int x = 0; x < total; x++) {
            int module_x = x / pixel_size - border;
            bool dark = false;
            if (module_x >= 0 && module_x < modules && module_y >= 0 && module_y < modules) {
                dark = qr.getModule(module_x, module_y);
            }
            unsigned char val = dark ? 0 : 255;
            int offset = y * row_bytes + x * 3;
            buf[offset] = val;     // B
            buf[offset + 1] = val; // G
            buf[offset + 2] = val; // R
        }
        // 填充对齐字节
        for (int x = total * 3; x < row_bytes; x++) {
            buf[y * row_bytes + x] = 0;
        }
    }
    return hBmp;
}

// ========== 卡片式 UI 绘制辅助函数 ==========

// 绘制渐变填充的圆角矩形 (用裁剪区域 + GradientFill)
static void DrawGradientRoundRect(HDC hdc, const RECT& rc, int radius,
                                   COLORREF c1, COLORREF c2, bool vertical) {
    HRGN hRgn = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1,
                                    radius, radius);
    SelectClipRgn(hdc, hRgn);
    TRIVERTEX vert[2] = {};
    vert[0].x = rc.left; vert[0].y = rc.top;
    vert[0].Red = (COLOR16)(GetRValue(c1) << 8);
    vert[0].Green = (COLOR16)(GetGValue(c1) << 8);
    vert[0].Blue = (COLOR16)(GetBValue(c1) << 8);
    vert[0].Alpha = 0;
    vert[1].x = rc.right; vert[1].y = rc.bottom;
    vert[1].Red = (COLOR16)(GetRValue(c2) << 8);
    vert[1].Green = (COLOR16)(GetGValue(c2) << 8);
    vert[1].Blue = (COLOR16)(GetBValue(c2) << 8);
    vert[1].Alpha = 0;
    GRADIENT_RECT gRect = {0, 1};
    GradientFill(hdc, vert, 2, &gRect, 1,
                 vertical ? GRADIENT_FILL_RECT_V : GRADIENT_FILL_RECT_H);
    SelectClipRgn(hdc, nullptr);
    DeleteObject(hRgn);
}

// 绘制纯色填充的圆角矩形 (带边框)
static void DrawSolidRoundRect(HDC hdc, const RECT& rc, int radius,
                                COLORREF fill, COLORREF border, int borderWidth = 1) {
    HBRUSH hBrush = CreateSolidBrush(fill);
    HPEN hPen = CreatePen(PS_SOLID, borderWidth, border);
    HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

// 绘制卡片 (白色圆角矩形 + 微妙阴影)
static void DrawCard(HDC hdc, const RECT& rc) {
    // 微妙阴影 (下方偏移2px + 模糊效果)
    RECT shadowRc = rc;
    OffsetRect(&shadowRc, 0, 2);
    DrawSolidRoundRect(hdc, shadowRc, Dpi(CARD_RADIUS),
                       RGB(231,236,247), RGB(231,236,247));
    // 卡片本体
    DrawSolidRoundRect(hdc, rc, Dpi(CARD_RADIUS),
                       RGB(255,255,255), RGB(234,238,247));
}

// 绘制卡片头部 (彩色图标方块 + 标题 + 右侧描述)
static void DrawCardHeader(HDC hdc, const RECT& cardRc, const wchar_t* title,
                           const wchar_t* desc, bool sendIcon) {
    int iconSize = Dpi(28);
    int iconX = cardRc.left + Dpi(CARD_PADDING);
    int iconY = cardRc.top + Dpi(CARD_PADDING);
    RECT iconRc = {iconX, iconY, iconX + iconSize, iconY + iconSize};

    // 图标方块: 渐变填充圆角
    if (sendIcon) {
        DrawGradientRoundRect(hdc, iconRc, 7, RGB(107,117,212), RGB(129,140,248), false);
    } else {
        DrawGradientRoundRect(hdc, iconRc, 7, RGB(14,165,233), RGB(56,189,248), false);
    }

    // 图标内箭头
    HFONT oldFont = (HFONT)SelectObject(hdc, g_ctx.hFontCard);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255,255,255));
    RECT iconTextRc = iconRc;
    DrawTextW(hdc, sendIcon ? L"\u2191" : L"\u2193", -1, &iconTextRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 标题文字
    int titleX = iconX + iconSize + Dpi(10);
    RECT titleRc = {titleX, iconY, cardRc.right - Dpi(CARD_PADDING), iconY + iconSize};
    SetTextColor(hdc, RGB(30,41,59));
    DrawTextW(hdc, title, -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 描述文字 (右侧灰色)
    if (desc) {
        SelectObject(hdc, g_ctx.hFontSmall);
        SetTextColor(hdc, RGB(148,163,184));
        DrawTextW(hdc, desc, -1, &titleRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);
}

// ========== 自绘长条胶囊进度条 ==========
// 格式化字节大小: B / KB / MB / GB (1 位小数)
static std::wstring FormatSize(uint64_t bytes) {
    double v = (double)bytes;
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    wchar_t buf[32];
    if (u == 0) swprintf_s(buf, L"%d B", (int)bytes);
    else        swprintf_s(buf, L"%.1f %s", v, units[u]);
    return buf;
}

// 进度文本: "65% · 13.0 / 20.0 MB"
static std::wstring FormatProgressText(uint64_t done, uint64_t total, int pct) {
    wchar_t buf[64];
    swprintf_s(buf, L"%d%% \u00b7 %s / %s", pct,
               FormatSize(done).c_str(), FormatSize(total).c_str());
    return buf;
}

// 绘制长条胶囊进度条 + 右侧进度文本 (在进度条卡片内)
// 布局: [进度条胶囊] [进度文本] [取消按钮]
static void DrawPillProgress(HDC hdc, const RECT& rcCard) {
    int pad = Dpi(CARD_PADDING);
    int barH = Dpi(16);
    int cancelW = Dpi(88);      // 与 DoLayout 中取消按钮宽度一致
    int textW = Dpi(190);       // 进度文本区域宽度
    int gap = Dpi(12);

    int right = rcCard.right - pad - cancelW - gap;
    RECT textRc = { right - textW, rcCard.top, right, rcCard.bottom };
    RECT barRc = {
        rcCard.left + pad,
        rcCard.top + (rcCard.bottom - rcCard.top - barH) / 2,
        textRc.left - gap,
        rcCard.top + (rcCard.bottom - rcCard.top + barH) / 2
    };
    int radius = barH / 2;

    // 胶囊背景
    DrawSolidRoundRect(hdc, barRc, radius, RGB(232,236,245), RGB(232,236,245));

    // 填充部分 (蓝紫渐变)
    int pct = g_ctx.prog_pct;
    if (pct > 0) {
        int fullW = barRc.right - barRc.left;
        int fillW = (int)((int64_t)fullW * pct / 100);
        if (fillW < radius * 2) fillW = radius * 2;   // 最小显示一段小胶囊
        if (fillW > fullW) fillW = fullW;
        if (fillW > 0) {
            RECT fillRc = barRc;
            fillRc.right = barRc.left + fillW;
            DrawGradientRoundRect(hdc, fillRc, radius, RGB(107,117,212), RGB(139,149,250), false);
        }
    }

    // 右侧进度文本
    HFONT oldF = (HFONT)SelectObject(hdc, g_ctx.hFontSmall);
    SetBkMode(hdc, TRANSPARENT);
    std::wstring text;
    if (g_ctx.prog_active && g_ctx.prog_total > 0) {
        text = FormatProgressText(g_ctx.prog_done, g_ctx.prog_total, g_ctx.prog_pct);
        SetTextColor(hdc, RGB(99,110,200));
    } else {
        text = L"\u51c6\u5907\u5c31\u7eea";  // 准备就绪
        SetTextColor(hdc, RGB(148,163,184));
    }
    DrawTextW(hdc, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);
}

// 重置进度条 UI 状态
static void ResetProgressUI() {
    g_ctx.prog_pct = 0;
    g_ctx.prog_done = 0;
    g_ctx.prog_total = 0;
    g_ctx.prog_active = false;
    if (g_ctx.hwnd) InvalidateRect(g_ctx.hwnd, &g_ctx.rcProgress, FALSE);
}

// 主窗口 WM_PAINT: 绘制背景、标题栏、标签栏、卡片
static void PaintMainWindow(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    int w = rcClient.right;

    // 1. 主背景
    HBRUSH hBgBrush = CreateSolidBrush(RGB(245,247,252));
    FillRect(hdc, &rcClient, hBgBrush);
    DeleteObject(hBgBrush);

    // 2. 标题栏渐变 (#7882dc -> #6b75d4)
    TRIVERTEX tv[2] = {};
    tv[0].x = 0; tv[0].y = 0;
    tv[0].Red = 120 << 8; tv[0].Green = 130 << 8; tv[0].Blue = 220 << 8; tv[0].Alpha = 0;
    tv[1].x = w; tv[1].y = Dpi(TITLE_BAR_H);
    tv[1].Red = 107 << 8; tv[1].Green = 117 << 8; tv[1].Blue = 212 << 8; tv[1].Alpha = 0;
    GRADIENT_RECT gr = {0, 1};
    GradientFill(hdc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_H);

    // 3. 标题文字 + 左侧 Logo
    {
        int logoSize = Dpi(28);
        int logoX = Dpi(14);
        int logoY = (Dpi(TITLE_BAR_H) - logoSize) / 2;
        // Logo 背景: 半透明白色圆角
        HBRUSH hLogoBg = CreateSolidBrush(RGB(255,255,255));
        HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
        HPEN oldPen = (HPEN)SelectObject(hdc, hNullPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hLogoBg);
        RoundRect(hdc, logoX, logoY, logoX + logoSize, logoY + logoSize, 5, 5);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hLogoBg);
        DeleteObject(hNullPen);

        // Logo 文字 "S"
        HFONT logoFont = CreateFontW(Dpi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     FF_DONTCARE, L"Segoe UI");
        HFONT oldLogoFont = (HFONT)SelectObject(hdc, logoFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(107,117,212));
        RECT logoTextRc = {logoX, logoY, logoX + logoSize, logoY + logoSize};
        DrawTextW(hdc, L"S", -1, &logoTextRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldLogoFont);
        DeleteObject(logoFont);
    }

    // 标题文字
    HFONT oldFont = (HFONT)SelectObject(hdc, g_ctx.hFontTitle);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255,255,255));
    RECT titleTextRc = {Dpi(44), 8, w - Dpi(MARGIN) - Dpi(90), Dpi(TITLE_BAR_H) - 8};
    DrawTextW(hdc, L"\u81f4\u4f20 Silex v0.1.0", -1, &titleTextRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 3.5 交通灯按钮 (macOS 风格)
    {
        HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
        HPEN oldPen = (HPEN)SelectObject(hdc, hNullPen);

        // 最小化 (黄色)
        HBRUSH hBrush;
        hBrush = CreateSolidBrush(g_ctx.hoverMin ? RGB(255,210,80) : RGB(255,200,61));
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        Ellipse(hdc, g_ctx.rcBtnMin.left, g_ctx.rcBtnMin.top,
                g_ctx.rcBtnMin.right, g_ctx.rcBtnMin.bottom);

        // 最大化 (绿色)
        hBrush = CreateSolidBrush(g_ctx.hoverMax ? RGB(100,220,120) : RGB(81,207,102));
        SelectObject(hdc, hBrush);
        Ellipse(hdc, g_ctx.rcBtnMax.left, g_ctx.rcBtnMax.top,
                g_ctx.rcBtnMax.right, g_ctx.rcBtnMax.bottom);

        // 关闭 (红色)
        hBrush = CreateSolidBrush(g_ctx.hoverCls ? RGB(255,120,120) : RGB(255,107,107));
        SelectObject(hdc, hBrush);
        Ellipse(hdc, g_ctx.rcBtnCls.left, g_ctx.rcBtnCls.top,
                g_ctx.rcBtnCls.right, g_ctx.rcBtnCls.bottom);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hNullPen);
    }

    // 4. 模式标签栏背景 (灰色圆角药丸)
    if (g_ctx.rcTabBar.right > g_ctx.rcTabBar.left) {
        DrawSolidRoundRect(hdc, g_ctx.rcTabBar, Dpi(TAB_H) - 6,
                           RGB(232,236,248), RGB(232,236,248));
    }

    // 5. 绘制卡片 + 卡片头部
    bool lan = (g_ctx.mode == TransferMode::LAN);

    // 卡片1 (发送)
    DrawCard(hdc, g_ctx.rcCard1);
    if (lan)
        DrawCardHeader(hdc, g_ctx.rcCard1, L"\u53d1\u9001\u6587\u4ef6",
                       L"\u81ea\u52a8\u53d1\u73b0\u63a5\u6536\u7aef", true);
    else
        DrawCardHeader(hdc, g_ctx.rcCard1, L"\u4e2d\u7ee7\u53d1\u9001",
                       L"\u521b\u5efa\u623f\u95f4", true);

    // 卡片2 (接收)
    DrawCard(hdc, g_ctx.rcCard2);
    if (lan)
        DrawCardHeader(hdc, g_ctx.rcCard2, L"\u63a5\u6536\u6587\u4ef6",
                       L"\u76f4\u8fde - \u670d\u52a1\u7aef", false);
    else
        DrawCardHeader(hdc, g_ctx.rcCard2, L"\u4e2d\u7ee7\u63a5\u6536",
                       L"\u8f93\u5165\u623f\u95f4\u7801", false);

    // 进度条卡片
    DrawCard(hdc, g_ctx.rcProgress);
    DrawPillProgress(hdc, g_ctx.rcProgress);

    // 日志卡片
    DrawCard(hdc, g_ctx.rcLog);

    // 日志卡片头部 (绿点 + "状态日志")
    if (g_ctx.rcLog.right > g_ctx.rcLog.left) {
        int dotSize = Dpi(10);
        int dotX = g_ctx.rcLog.left + Dpi(CARD_PADDING);
        int dotY = g_ctx.rcLog.top + Dpi(16);
        HBRUSH hDotBrush = CreateSolidBrush(RGB(34,197,94));
        HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
        HPEN oldPen = (HPEN)SelectObject(hdc, hNullPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hDotBrush);
        Ellipse(hdc, dotX, dotY, dotX + dotSize, dotY + dotSize);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hNullPen);
        DeleteObject(hDotBrush);

        SelectObject(hdc, g_ctx.hFontCard);
        SetTextColor(hdc, RGB(100,116,139));
        RECT logTitleRc = {dotX + dotSize + Dpi(8), g_ctx.rcLog.top + Dpi(8),
                           g_ctx.rcLog.right - Dpi(CARD_PADDING), g_ctx.rcLog.top + Dpi(36)};
        DrawTextW(hdc, L"\u72b6\u6001\u65e5\u5fd7", -1, &logTitleRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldFont);

    EndPaint(hWnd, &ps);
}

// Owner-Draw 按钮绘制
static void DrawODButton(DRAWITEMSTRUCT* dis) {
    if (dis->CtlType != ODT_BUTTON) return;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int id = dis->CtlID;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    wchar_t text[64] = {};
    GetWindowTextW(dis->hwndItem, text, 64);

    HFONT oldFont = (HFONT)SelectObject(hdc, g_ctx.hFontBtn);
    SetBkMode(hdc, TRANSPARENT);

    if (id == IDC_MODE_LAN || id == IDC_MODE_RELAY) {
        // ===== Tab 按钮 (专用大字字体) =====
        SelectObject(hdc, g_ctx.hFontTab);
        bool selected = (id == IDC_MODE_LAN && g_ctx.mode == TransferMode::LAN) ||
                        (id == IDC_MODE_RELAY && g_ctx.mode == TransferMode::RELAY);
        bool hover = (id == IDC_MODE_LAN && g_ctx.hoverTabLan) ||
                     (id == IDC_MODE_RELAY && g_ctx.hoverTabRelay);

        if (selected) {
            // 选中态: 白色卡片 + 阴影
            RECT shadowRc = rc;
            OffsetRect(&shadowRc, 0, 1);
            DrawSolidRoundRect(hdc, shadowRc, 8, RGB(200,210,235), RGB(200,210,235));
            DrawSolidRoundRect(hdc, rc, 8, RGB(255,255,255), RGB(255,255,255));
        } else if (pressed) {
            // 按下态: 深蓝紫色
            DrawSolidRoundRect(hdc, rc, 8, RGB(222,226,245), RGB(222,226,245));
        } else if (hover) {
            // 悬停态: 浅灰色
            DrawSolidRoundRect(hdc, rc, 8, RGB(240,243,250), RGB(240,243,250));
        } else {
            // 普通态: 与标签栏背景一致
            DrawSolidRoundRect(hdc, rc, 8, RGB(232,236,248), RGB(232,236,248));
        }
        SetTextColor(hdc, selected ? RGB(107,117,212) : RGB(107,114,128));
        RECT textRc = rc;
        DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, g_ctx.hFontBtn); // 恢复默认按钮字体

    } else if (id == IDC_SEND_BTN || id == IDC_RECV_BTN ||
               id == IDC_RSEND_BTN || id == IDC_RRECV_BTN) {
        // ===== 主按钮: 蓝紫渐变 =====
        bool hover = (id == IDC_SEND_BTN && g_ctx.hoverSend) ||
                     (id == IDC_RECV_BTN && g_ctx.hoverRecv) ||
                     (id == IDC_RSEND_BTN && g_ctx.hoverRSend) ||
                     (id == IDC_RRECV_BTN && g_ctx.hoverRRecv);

        COLORREF c1, c2;
        if (disabled) { c1 = RGB(170,175,210); c2 = RGB(190,195,225); }
        else if (pressed) { c1 = RGB(91,101,192); c2 = RGB(109,120,218); }
        else if (hover) { c1 = RGB(117,127,222); c2 = RGB(149,160,255); }
        else { c1 = RGB(107,117,212); c2 = RGB(129,140,248); }

        DrawGradientRoundRect(hdc, rc, 8, c1, c2, false);
        SetTextColor(hdc, RGB(255,255,255));
        RECT textRc = rc;
        if (pressed) OffsetRect(&textRc, 1, 1);
        DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    } else {
        // ===== 浏览/次要按钮: 白底浅边框 =====
        bool hover = (id == IDC_FILE_BROWSE && g_ctx.hoverBrowse1) ||
                     (id == IDC_DIR_BROWSE && g_ctx.hoverBrowse2) ||
                     (id == IDC_RSEND_FILE_BROWSE && g_ctx.hoverBrowse3) ||
                     (id == IDC_RRECV_DIR_BROWSE && g_ctx.hoverBrowse4) ||
                     (id == IDC_RSEND_ADV_BTN && g_ctx.hoverAdv) ||
                     (id == IDC_CANCEL_BTN && g_ctx.hoverCancel);

        COLORREF fill, border, textColor;
        if (disabled) {
            fill = RGB(245,247,252); border = RGB(224,230,240); textColor = RGB(180,190,200);
        } else if (pressed) {
            fill = RGB(238,242,250); border = RGB(165,180,252); textColor = RGB(107,117,212);
        } else if (hover) {
            fill = RGB(245,248,255); border = RGB(165,180,252); textColor = RGB(107,117,212);
        } else {
            fill = RGB(255,255,255); border = RGB(224,230,240); textColor = RGB(100,116,139);
        }

        DrawSolidRoundRect(hdc, rc, 8, fill, border);
        SetTextColor(hdc, textColor);
        RECT textRc = rc;
        if (pressed) OffsetRect(&textRc, 1, 1);
        DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldFont);
}

// 更新所有按钮的悬停状态 (通过定时器调用)
static void UpdateHoverStates() {
    POINT pt;
    GetCursorPos(&pt);
    HWND hOver = WindowFromPoint(pt);

    struct { HWND hwnd; bool* flag; } checks[] = {
        {g_ctx.hModeLan, &g_ctx.hoverTabLan},
        {g_ctx.hModeRelay, &g_ctx.hoverTabRelay},
        {g_ctx.hSendBtn, &g_ctx.hoverSend},
        {g_ctx.hRecvBtn, &g_ctx.hoverRecv},
        {g_ctx.hRSendBtn, &g_ctx.hoverRSend},
        {g_ctx.hRRecvBtn, &g_ctx.hoverRRecv},
        {g_ctx.hFileBrowse, &g_ctx.hoverBrowse1},
        {g_ctx.hDirBrowse, &g_ctx.hoverBrowse2},
        {g_ctx.hRSendFileBrowse, &g_ctx.hoverBrowse3},
        {g_ctx.hRRecvDirBrowse, &g_ctx.hoverBrowse4},
        {g_ctx.hRSendAdvBtn, &g_ctx.hoverAdv},
        {g_ctx.hCancelBtn, &g_ctx.hoverCancel},
    };

    for (auto& c : checks) {
        bool newHover = (hOver == c.hwnd);
        if (newHover != *c.flag) {
            *c.flag = newHover;
            if (c.hwnd && IsWindowVisible(c.hwnd))
                InvalidateRect(c.hwnd, nullptr, FALSE);
        }
    }

    // 交通灯按钮 hover 检测 (在主窗口客户区)
    POINT ptClient = pt;
    if (ScreenToClient(g_ctx.hwnd, &ptClient)) {
        bool newMin = PtInRect(&g_ctx.rcBtnMin, ptClient);
        bool newMax = PtInRect(&g_ctx.rcBtnMax, ptClient);
        bool newCls = PtInRect(&g_ctx.rcBtnCls, ptClient);
        if (newMin != g_ctx.hoverMin || newMax != g_ctx.hoverMax || newCls != g_ctx.hoverCls) {
            g_ctx.hoverMin = newMin;
            g_ctx.hoverMax = newMax;
            g_ctx.hoverCls = newCls;
            // 只刷新标题栏区域, 避免整个窗口闪烁
            RECT rcTitle = {0, 0, g_ctx.rcTabBar.right, Dpi(TITLE_BAR_H)};
            InvalidateRect(g_ctx.hwnd, &rcTitle, FALSE);
        }
    }
}

// ========== 窗口过程 ==========
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 注册 Windows 防火墙规则 (允许本程序接收入站 TCP 连接)
        // 启动时非阻塞触发 UAC; 若被拒绝, 进入中继发送模式时会再次重试并等待
        EnsureFirewallRule(false);

        // 创建字体 (按 DPI 缩放, 96 DPI 基准)
        g_dpiScale = GetDpiForWindow(hWnd) / 96.0f;
        g_ctx.hFont = CreateFontW(Dpi(19), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  FF_DONTCARE, L"Microsoft YaHei UI");
        // 卡片式 UI 专用字体
        g_ctx.hFontTitle = CreateFontW(Dpi(24), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       FF_DONTCARE, L"Microsoft YaHei UI");
        g_ctx.hFontCard = CreateFontW(Dpi(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      FF_DONTCARE, L"Microsoft YaHei UI");
        g_ctx.hFontSmall = CreateFontW(Dpi(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       FF_DONTCARE, L"Microsoft YaHei UI");
        g_ctx.hFontBtn = CreateFontW(Dpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     FF_DONTCARE, L"Microsoft YaHei UI");
        g_ctx.hFontTab = CreateFontW(Dpi(21), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     FF_DONTCARE, L"Microsoft YaHei UI");

        // 标题由 WM_PAINT 直接绘制, 不创建 static 控件
        g_ctx.hTitle = nullptr;

        // ===== 模式标签 (Owner-Draw Tab 按钮, 替代 RadioButton) =====
        // GroupBox 已移除 (hModeGroup 保持 nullptr)
        g_ctx.hModeGroup = nullptr;
        g_ctx.hModeLan = CreateCtrl(hWnd, L"button", L"\u5c40\u57df\u7f51\u76f4\u8fde",
                   BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 10, 10, IDC_MODE_LAN);
        g_ctx.hModeRelay = CreateCtrl(hWnd, L"button", L"\u623f\u95f4\u7801\u4e2d\u7ee7",
                   BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 10, 10, IDC_MODE_RELAY);

        // ===== 局域网直连 - 发送区域 (GroupBox 已移除) =====
        g_ctx.hSendGroup = nullptr;
        g_ctx.hSendPortLbl = CreateCtrl(hWnd, L"static", L"\u7aef\u53e3", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hSendPortEdit = CreateEditCentered(hWnd, nullptr, L"9090", 0, IDC_SEND_PORT_EDIT);
        g_ctx.hFileLbl = CreateCtrl(hWnd, L"static", L"\u6587\u4ef6\u8def\u5f84", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hFileEdit = CreateEditCentered(hWnd, L"\u9009\u62e9\u8981\u53d1\u9001\u7684\u6587\u4ef6", L"", 0, IDC_FILE_EDIT);
        g_ctx.hFileBrowse = CreateCtrl(hWnd, L"button", L"\u6d4f\u89c8...",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_FILE_BROWSE);
        g_ctx.hSendBtn = CreateCtrl(hWnd, L"button", L"\u2191 \u53d1\u9001",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_SEND_BTN);

        // ===== 局域网直连 - 接收区域 =====
        g_ctx.hRecvGroup = nullptr;
        g_ctx.hRecvPortLbl = CreateCtrl(hWnd, L"static", L"\u7aef\u53e3", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRecvPortEdit = CreateEditCentered(hWnd, nullptr, L"9090", 0, IDC_RECV_PORT_EDIT);
        g_ctx.hDirLbl = CreateCtrl(hWnd, L"static", L"\u4fdd\u5b58\u76ee\u5f55", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hDirEdit = CreateEditCentered(hWnd, L"\u7559\u7a7a\u5219\u4fdd\u5b58\u5230\u7a0b\u5e8f\u6240\u5728\u76ee\u5f55", L"", 0, IDC_DIR_EDIT);
        g_ctx.hDirBrowse = CreateCtrl(hWnd, L"button", L"\u6d4f\u89c8...",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_DIR_BROWSE);
        g_ctx.hRecvBtn = CreateCtrl(hWnd, L"button", L"\u2193 \u5f00\u59cb\u63a5\u6536",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_RECV_BTN);

        // ===== 中继 - 发送方 =====
        g_ctx.hRSendGroup = nullptr;
        g_ctx.hRSendFileLbl = CreateCtrl(hWnd, L"static", L"\u6587\u4ef6\u8def\u5f84", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRSendFileEdit = CreateEditCentered(hWnd, L"\u9009\u62e9\u8981\u53d1\u9001\u7684\u6587\u4ef6", L"", 0, IDC_RSEND_FILE_EDIT);
        g_ctx.hRSendFileBrowse = CreateCtrl(hWnd, L"button", L"\u6d4f\u89c8...",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_RSEND_FILE_BROWSE);
        g_ctx.hRSendCodeLbl = CreateCtrl(hWnd, L"static", L"\u623f\u95f4\u7801", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRSendCodeEdit = CreateEditCentered(hWnd, nullptr, L"(\u521b\u5efa\u540e\u663e\u793a)", ES_READONLY, IDC_RSEND_CODE_EDIT);
        g_ctx.hRSendBtn = CreateCtrl(hWnd, L"button", L"\u521b\u5efa\u623f\u95f4\u5e76\u53d1\u9001",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_RSEND_BTN);
        g_ctx.hRSendAdvBtn = CreateCtrl(hWnd, L"button", L"\u9ad8\u7ea7\u8bbe\u7f6e",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_RSEND_ADV_BTN);

        // ===== 中继 - 接收方 =====
        g_ctx.hRRecvGroup = nullptr;
        g_ctx.hRRecvCodeLbl = CreateCtrl(hWnd, L"static", L"\u623f\u95f4\u7801", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRRecvCodeEdit = CreateEditCentered(hWnd, L"6 \u4f4d\u5b57\u6bcd\u6570\u5b57", L"", 0, IDC_RRECV_CODE_EDIT);
        g_ctx.hRRecvDirLbl = CreateCtrl(hWnd, L"static", L"\u4fdd\u5b58\u76ee\u5f55", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hRRecvDirEdit = CreateEditCentered(hWnd, L"\u7559\u7a7a\u5219\u4fdd\u5b58\u5230\u7a0b\u5e8f\u6240\u5728\u76ee\u5f55", L"", 0, IDC_RRECV_DIR_EDIT);
        g_ctx.hRRecvDirBrowse = CreateCtrl(hWnd, L"button", L"\u6d4f\u89c8...",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_RRECV_DIR_BROWSE);
        g_ctx.hRRecvBtn = CreateCtrl(hWnd, L"button", L"\u52a0\u5165\u623f\u95f4\u5e76\u63a5\u6536",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_RRECV_BTN);

        // ===== 中继发送二维码显示区 =====
        g_ctx.hQrImage = CreateCtrl(hWnd, L"static",
                   L"\u626b\u7801\u81ea\u52a8\u63a5\u6536",
                   SS_CENTER | WS_BORDER, 0, 0, 10, 10, 0);
        SendMessageW(g_ctx.hQrImage, WM_SETFONT, (WPARAM)g_ctx.hFontSmall, TRUE);
        g_ctx.hQrCodeLbl2 = CreateCtrl(hWnd, L"static",
                   L"\u626b\u7801\u540e\u624b\u673a\u5c06\u81ea\u52a8\u8fde\u63a5\u672c\u7535\u8111\u63a5\u6536\u6587\u4ef6",
                   SS_LEFT, 0, 0, 10, 10, 0);
        SendMessageW(g_ctx.hQrCodeLbl2, WM_SETFONT, (WPARAM)g_ctx.hFontSmall, TRUE);

        // ===== 进度 + 取消 =====
        // 进度条为自绘长条胶囊 (WM_PAINT 中绘制), 无需创建标准控件
        g_ctx.hCancelBtn = CreateCtrl(hWnd, L"button", L"\u53d6\u6d88",
                   BS_OWNERDRAW, 0, 0, 10, 10, IDC_CANCEL_BTN);
        EnableWindow(g_ctx.hCancelBtn, FALSE);

        // ===== 日志 =====
        g_ctx.hLogLbl = CreateCtrl(hWnd, L"static", L"\u72b6\u6001\u65e5\u5fd7", SS_LEFT, 0, 0, 10, 10, 0);
        g_ctx.hLog = CreateCtrl(hWnd, L"edit", L"",
                   ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
                   WS_VSCROLL | WS_BORDER, 0, 0, 10, 10, IDC_LOG);

        // 为标签设置小号字体 (CreateCtrl 默认用 hFont, 此处覆盖)
        auto SetSmallFont = [](HWND h) {
            if (h && g_ctx.hFontSmall) SendMessageW(h, WM_SETFONT, (WPARAM)g_ctx.hFontSmall, TRUE);
        };
        SetSmallFont(g_ctx.hSendPortLbl);  SetSmallFont(g_ctx.hFileLbl);
        SetSmallFont(g_ctx.hRecvPortLbl);  SetSmallFont(g_ctx.hDirLbl);
        SetSmallFont(g_ctx.hRSendFileLbl); SetSmallFont(g_ctx.hRSendCodeLbl);
        SetSmallFont(g_ctx.hRRecvCodeLbl); SetSmallFont(g_ctx.hRRecvDirLbl);

        // 为编辑框设置正文字体 (与正文一致, 不再用按钮字体)
        auto SetEditFont = [](HWND h) {
            if (h && g_ctx.hFont) SendMessageW(h, WM_SETFONT, (WPARAM)g_ctx.hFont, TRUE);
        };
        SetEditFont(g_ctx.hSendPortEdit); SetEditFont(g_ctx.hFileEdit);
        SetEditFont(g_ctx.hRecvPortEdit); SetEditFont(g_ctx.hDirEdit);
        SetEditFont(g_ctx.hRSendFileEdit); SetEditFont(g_ctx.hRSendCodeEdit);
        SetEditFont(g_ctx.hRRecvCodeEdit); SetEditFont(g_ctx.hRRecvDirEdit);
        SetEditFont(g_ctx.hLog);

        // 让输入框文字垂直居中 (格式化矩形收窄为一行并垂直居中)
        CenterEditTextVertically(g_ctx.hSendPortEdit);
        CenterEditTextVertically(g_ctx.hFileEdit);
        CenterEditTextVertically(g_ctx.hRecvPortEdit);
        CenterEditTextVertically(g_ctx.hDirEdit);
        CenterEditTextVertically(g_ctx.hRSendFileEdit);
        CenterEditTextVertically(g_ctx.hRSendCodeEdit);
        CenterEditTextVertically(g_ctx.hRRecvCodeEdit);
        CenterEditTextVertically(g_ctx.hRRecvDirEdit);

        // 悬停状态追踪定时器 (50ms 间隔, 用于 owner-draw 按钮 hover 效果)
        SetTimer(hWnd, 1, 50, nullptr);

        // 初始模式可见性
        ApplyModeVisibility();

        AppendLog(L"\u5c31\u7eea\u3002\u8bf7\u9009\u62e9\u4f20\u8f93\u6a21\u5f0f (\u5c40\u57df\u7f51\u76f4\u8fde / \u623f\u95f4\u7801\u4e2d\u7ee7)\u3002\r\n");
        AppendLog(L"\u63d0\u793a: \u623f\u95f4\u7801\u4e2d\u7ee7\u6a21\u5f0f\u9700\u5148\u5728\u516c\u7f51 VPS \u4e0a\u8fd0\u884c SilexRelay.exe\r\n");
        AppendLog(L"\u63d0\u793a: \u4e2d\u7ee7\u53d1\u9001\u65f6\u4f1a\u5728\u65c1\u8fb9\u663e\u793a\u4e8c\u7ef4\u7801, \u624b\u673a\u626b\u7801\u53ef\u81ea\u52a8\u52a0\u5165\u623f\u95f4\r\n");
        return 0;
    }

    case WM_SIZE:
        DoLayout(LOWORD(lParam), HIWORD(lParam));
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_NCCALCSIZE: {
        // 移除非客户区, 使客户区覆盖整个窗口
        if (wParam) {
            // wParam == TRUE: lParam 是 NCCALCSIZE_PARAMS*
            // 返回 0 表示接受系统提议的客户区
            return 0;
        }
        // wParam == FALSE: lParam 是 RECT*, 返回 0 接受
        return 0;
    }

    case WM_NCPAINT:
        // 阻止系统绘制非客户区边框
        return 0;

    case WM_NCACTIVATE:
        // 阻止系统绘制非激活边框
        return TRUE;

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = Dpi(MIN_W);
        mmi->ptMinTrackSize.y = Dpi(MIN_H);
        return 0;
    }

    case WM_NCHITTEST: {
        // 检查鼠标是否在交通灯按钮上
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINT ptClient = pt;
        ScreenToClient(hWnd, &ptClient);

        if (PtInRect(&g_ctx.rcBtnMin, ptClient)) return HTCLIENT;
        if (PtInRect(&g_ctx.rcBtnMax, ptClient)) return HTCLIENT;
        if (PtInRect(&g_ctx.rcBtnCls, ptClient)) return HTCLIENT;

        // 标题栏区域 (可拖动)
        RECT rcTitle = { 0, 0, g_ctx.rcTabBar.right, Dpi(TITLE_BAR_H) };
        if (PtInRect(&rcTitle, ptClient)) return HTCAPTION;

        // 窗口边缘 (可调整大小)
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int x = pt.x - rc.left;
        int y = pt.y - rc.top;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int bw = 8;  // 边缘宽度
        if (x < bw && y < bw)   return HTTOPLEFT;
        if (x > w-bw && y < bw) return HTTOPRIGHT;
        if (x < bw && y > h-bw) return HTBOTTOMLEFT;
        if (x > w-bw && y > h-bw) return HTBOTTOMRIGHT;
        if (x < bw) return HTLEFT;
        if (x > w-bw) return HTRIGHT;
        if (y < bw) return HTTOP;
        if (y > h-bw) return HTBOTTOM;

        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&g_ctx.rcBtnMin, pt)) {
            ShowWindow(hWnd, SW_MINIMIZE);
            return 0;
        }
        if (PtInRect(&g_ctx.rcBtnMax, pt)) {
            if (g_ctx.maximized) {
                // 还原
                SetWindowPos(hWnd, nullptr, g_ctx.rcRestore.left, g_ctx.rcRestore.top,
                    g_ctx.rcRestore.right - g_ctx.rcRestore.left,
                    g_ctx.rcRestore.bottom - g_ctx.rcRestore.top,
                    SWP_NOZORDER);
                g_ctx.maximized = false;
                // 重新应用 DWM 圆角
                DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
                DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
            } else {
                // 最大化
                RECT rcWnd;
                GetWindowRect(hWnd, &rcWnd);
                g_ctx.rcRestore = rcWnd;
                // 获取显示器工作区
                HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                GetMonitorInfoW(hMon, &mi);
                SetWindowPos(hWnd, nullptr,
                    mi.rcWork.left, mi.rcWork.top,
                    mi.rcWork.right - mi.rcWork.left,
                    mi.rcWork.bottom - mi.rcWork.top,
                    SWP_NOZORDER);
                g_ctx.maximized = true;
                // 最大化时移除圆角
                DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_DONOTROUND;
                DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
            }
            return 0;
        }
        if (PtInRect(&g_ctx.rcBtnCls, pt)) {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    case WM_ERASEBKGND:
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    case WM_PAINT:
        PaintMainWindow(hWnd);
        return 0;

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        DrawODButton(dis);
        return TRUE;
    }

    case WM_TIMER:
        if (wParam == 1) UpdateHoverStates();
        return 0;

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        int id = GetDlgCtrlID((HWND)lParam);
        // Tab 按钮背景匹配标签栏灰色, 其他按钮匹配卡片白色
        if (id == IDC_MODE_LAN || id == IDC_MODE_RELAY) {
            static HBRUSH hTabBg = CreateSolidBrush(RGB(232,236,248));
            return (INT_PTR)hTabBg;
        }
        static HBRUSH hBtnBg = CreateSolidBrush(RGB(255,255,255));
        return (INT_PTR)hBtnBg;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        if (msg == WM_CTLCOLOREDIT) {
            SetTextColor(hdc, RGB(51,65,85));
            // 编辑框: 浅灰背景
            static HBRUSH hEditBg = CreateSolidBrush(RGB(248,250,252));
            return (INT_PTR)hEditBg;
        }
        // 静态控件: 白色背景
        SetTextColor(hdc, RGB(71,85,105));
        static HBRUSH hCardBg = CreateSolidBrush(RGB(255,255,255));
        return (INT_PTR)hCardBg;
    }

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
                    AppendLog(L"[高级设置] 已切换到自定义中继服务器\r\n");
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
            ResetProgressUI();
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
            ResetProgressUI();
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
            ResetProgressUI();
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
            ResetProgressUI();
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
                // 保存进度状态, 由自绘进度条显示
                g_ctx.prog_total = pm->total;
                g_ctx.prog_done = pm->done;
                g_ctx.prog_pct = (int)(100 * pm->done / pm->total);
                if (g_ctx.prog_pct > 100) g_ctx.prog_pct = 100;
                g_ctx.prog_active = true;
                InvalidateRect(hWnd, &g_ctx.rcProgress, FALSE);
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

    case WM_APP_QR_UPDATE: {
        auto* qr_data = reinterpret_cast<std::string*>(lParam);
        if (qr_data) {
            g_ctx.qr_data = *qr_data;
            if (g_ctx.qr_bitmap) { DeleteObject(g_ctx.qr_bitmap); g_ctx.qr_bitmap = nullptr; }
            g_ctx.qr_bitmap = GenerateQrBitmap(g_ctx.qr_data, 6, g_ctx.qr_bitmap_size);
            if (g_ctx.qr_bitmap) {
                SetWindowTextW(g_ctx.hQrImage, L"");
                LONG_PTR style = GetWindowLongPtrW(g_ctx.hQrImage, GWL_STYLE);
                style = (style & ~SS_TYPEMASK) | SS_BITMAP;
                SetWindowLongPtrW(g_ctx.hQrImage, GWL_STYLE, style);
                SendMessageW(g_ctx.hQrImage, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_ctx.qr_bitmap);
                ShowWindow(g_ctx.hQrImage, SW_SHOW);
                RECT rc;
                GetClientRect(g_ctx.hwnd, &rc);
                DoLayout(rc.right, rc.bottom);
                InvalidateRect(g_ctx.hwnd, &g_ctx.rcCard1, FALSE);
            }
            std::wstring hint = L"扫码自动接收 (房间码已编码到二维码)";
            SetWindowTextW(g_ctx.hQrCodeLbl2, hint.c_str());
            ShowWindow(g_ctx.hQrCodeLbl2, SW_SHOW);
            delete qr_data;
        }
        return 0;
    }

    case WM_APP_DONE: {
        if (g_ctx.worker.joinable()) g_ctx.worker.join();
        // 复位忙碌标志, 允许再次发起传输 (否则按钮点击会被 busy 拦截)
        g_ctx.busy = false;
        g_ctx.cancel = false;
        // 停止接收端的 UDP 发现响应线程
        if (g_ctx.discovery_worker.joinable()) {
            g_ctx.discovery_running = false;
            g_ctx.discovery_worker.join();
        }
        // 清理内嵌二维码显示
        if (g_ctx.qr_bitmap) {
            DeleteObject(g_ctx.qr_bitmap);
            g_ctx.qr_bitmap = nullptr;
        }
        g_ctx.qr_data.clear();
        // 重置 QR 图片控件为初始提示文本
        SendMessageW(g_ctx.hQrImage, STM_SETIMAGE, IMAGE_BITMAP, 0);
        LONG_PTR style = GetWindowLongPtrW(g_ctx.hQrImage, GWL_STYLE);
        style = (style & ~SS_TYPEMASK) | SS_CENTER;
        SetWindowLongPtrW(g_ctx.hQrImage, GWL_STYLE, style);
        SetWindowTextW(g_ctx.hQrImage, L"扫码自动接收");
        SetWindowTextW(g_ctx.hQrCodeLbl2, L"扫码后手机将自动连接本电脑接收文件");
        SetWindowTextW(g_ctx.hRSendCodeEdit, L"（创建后显示）");
        ShowWindow(g_ctx.hQrImage, SW_SHOW);
        ShowWindow(g_ctx.hQrCodeLbl2, SW_SHOW);
        g_ctx.qr_listen_port = 0;
        SetTransferControls(TRUE);
        int ret = (int)wParam;
        if (ret == 0) {
            g_ctx.prog_pct = 100;
            g_ctx.prog_done = g_ctx.prog_total;
            InvalidateRect(hWnd, &g_ctx.rcProgress, FALSE);
            AppendLog(L"\r\n[完成] 传输成功!\r\n");
            MessageBoxW(hWnd, L"传输完成!", L"成功", MB_OK | MB_ICONINFORMATION);
        } else if (ret == ft::CANCELED) {
            AppendLog(L"\r\n[已取消] 传输已终止\r\n");
        } else {
            AppendLog(L"\r\n[失败] 传输出错, 错误码: " + std::to_wstring(ret) + L"\r\n");
            MessageBoxW(hWnd, L"传输失败, 请查看日志了解详情",
                        L"失败", MB_OK | MB_ICONERROR);
        }
        // 清理传输相关的 UI 内容 (房间码、文件路径、进度条)
        // 让用户可以立即开始新的传输
        ResetProgressUI();
        SetWindowTextW(g_ctx.hRRecvCodeEdit, L"6位字母数字");
        SetWindowTextW(g_ctx.hRSendFileEdit, L"选择要发送的文件");
        SetWindowTextW(g_ctx.hFileEdit, L"选择要发送的文件");
        // 注意: 保留保存目录方便用户重复使用
        EnableWindow(g_ctx.hCancelBtn, FALSE);
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
        KillTimer(hWnd, 1);
        if (g_ctx.qr_bitmap) {
            DeleteObject(g_ctx.qr_bitmap);
            g_ctx.qr_bitmap = nullptr;
        }
        TrayDelete();
        if (g_ctx.hFont) DeleteObject(g_ctx.hFont);
        if (g_ctx.hFontTitle) DeleteObject(g_ctx.hFontTitle);
        if (g_ctx.hFontCard) DeleteObject(g_ctx.hFontCard);
        if (g_ctx.hFontSmall) DeleteObject(g_ctx.hFontSmall);
        if (g_ctx.hFontBtn) DeleteObject(g_ctx.hFontBtn);
        if (g_ctx.hFontTab) DeleteObject(g_ctx.hFontTab);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ========== 入口 ==========
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // 单实例检测: 防止同一台电脑运行多个客户端
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Silex_Client_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        HWND existing = FindWindowW(L"SilexMainWindow", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    // 启用 DPI 感知, 避免高 DPI 屏幕上控件错位/遮挡
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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

    const wchar_t* cls_name = L"SilexMainWindow";
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

    // 自定义窗口样式: 无边框 + 可调整大小 + 最小化/最大化
    DWORD winStyle = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;

    // 计算窗口大小 (客户区 900x860)
    RECT rc = {0, 0, 900, 860};

    g_ctx.hwnd = CreateWindowExW(
        0, cls_name, L"Silex",
        winStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);

    // DWM 圆角窗口 (Windows 10 1803+)
    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_ctx.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    // 启用暗色标题栏, 使自定义标题栏与系统融合
    BOOL darkTitle = TRUE;
    DwmSetWindowAttribute(g_ctx.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitle, sizeof(darkTitle));

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
            } else if (g_ctx.mode == TransferMode::RELAY) {
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
