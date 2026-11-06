// Vikey for Windows: tray icon (V / E), menu, and the message loop the hooks need.
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <windowsx.h>
#include <algorithm>
#include <objidl.h>
#define GDIPVER 0x0110
#include <gdiplus.h>
using namespace Gdiplus;
#include "Core.h"
#include "Prefs.h"
#include "Site.h"
#include "Settings.h"
#include "CrashReporter.h"
#include "../../Engine/Vietnamese.h"
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

static const UINT WM_TRAY = WM_APP + 1;
static const UINT WM_VIKEY_SETLANG = WM_APP + 2;   // tests: wParam = language (0 English, 1 Vietnamese)
static const UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");   // Explorer (re)started: the tray is empty
static const UINT TIMER_FLUSH = 1;
static const UINT TIMER_TRAY_DPI = 2;
enum { ID_LOGTOGGLE = 100, ID_LOG, ID_ABOUT, ID_QUIT, ID_GUIDE };

static HWND _hwnd; static NOTIFYICONDATAW _nid = {};
static HICON _iconV, _iconE;
static ULONG_PTR _gdipToken = 0;
static UINT _iconDpi = 0;
static int _iconSize = 0;
static bool _trayAdded = false;

/// Render a crisp, DPI-sized tray tile. V uses the app's indigo gradient; E is black.
static HICON makeIcon(wchar_t letter, int size) {
    if (size <= 0) return nullptr;
    Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Graphics g(&bitmap);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));
    float inset = std::max(0.5f, size * 0.055f);
    RectF tile(inset, inset, size - inset * 2.0f, size - inset * 2.0f);
    GraphicsPath path;
    float d = std::min(tile.Width, tile.Height) * 0.46f;
    path.AddArc(tile.X, tile.Y, d, d, 180, 90);
    path.AddArc(tile.X + tile.Width - d, tile.Y, d, d, 270, 90);
    path.AddArc(tile.X + tile.Width - d, tile.Y + tile.Height - d, d, d, 0, 90);
    path.AddArc(tile.X, tile.Y + tile.Height - d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush tileFill(Color(255, 248, 249, 253));
    Pen tileBorder(Color(255, 205, 211, 230), std::max(0.7f, size * 0.045f));
    g.FillPath(&tileFill, &path);
    g.DrawPath(&tileBorder, &path);
    FontFamily family(L"Segoe UI");
    Font font(&family, std::max(1.0f, size * 0.69f), FontStyleBold, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    RectF textRect(0.0f, -size * 0.015f, (float)size, (float)size);
    if (letter == L'V') {
        LinearGradientBrush glyph(RectF(0, 0, (float)size, (float)size),
            Color(255, 83, 80, 205), Color(255, 55, 69, 184), LinearGradientModeVertical);
        g.DrawString(L"V", 1, &font, textRect, &format, &glyph);
    } else {
        SolidBrush glyph(Color(255, 0, 0, 0));
        g.DrawString(L"E", 1, &font, textRect, &format, &glyph);
    }
    HICON icon = nullptr;
    if (bitmap.GetHICON(&icon) != Ok) return nullptr;
    return icon;
}
static UINT trayDpi() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    typedef UINT (WINAPI *GetDpiForSystemFn)();
    auto getDpiForWindow = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tray && getDpiForWindow) {
        UINT dpi = getDpiForWindow(tray);
        if (dpi) return dpi;
    }
    auto getDpiForSystem = (GetDpiForSystemFn)GetProcAddress(user32, "GetDpiForSystem");
    if (getDpiForSystem) {
        UINT dpi = getDpiForSystem();
        if (dpi) return dpi;
    }
    return 96;
}
static int trayIconSize(UINT dpi) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef int (WINAPI *GetSystemMetricsForDpiFn)(int, UINT);
    auto getMetrics = (GetSystemMetricsForDpiFn)GetProcAddress(user32, "GetSystemMetricsForDpi");
    int size = getMetrics ? getMetrics(SM_CXSMICON, dpi) : GetSystemMetrics(SM_CXSMICON);
    return size > 0 ? size : 16;
}
static bool refreshTrayIcons(bool force = false) {
    const UINT dpi = trayDpi();
    const int size = trayIconSize(dpi);
    if (!force && _iconV && _iconE && dpi == _iconDpi && size == _iconSize) return true;
    HICON nextV = makeIcon(L'V', size);
    HICON nextE = makeIcon(L'E', size);
    if (!nextV || !nextE) {
        if (nextV) DestroyIcon(nextV);
        if (nextE) DestroyIcon(nextE);
        return false;
    }
    HICON oldV = _iconV, oldE = _iconE;
    _iconV = nextV; _iconE = nextE;
    _iconDpi = dpi; _iconSize = size;
    if (_trayAdded) {
        _nid.hIcon = vikey::core::language() ? _iconV : _iconE;
        Shell_NotifyIconW(NIM_MODIFY, &_nid);
    }
    if (oldV) DestroyIcon(oldV);
    if (oldE) DestroyIcon(oldE);
    return true;
}
static void destroyTrayIcons() {
    if (_iconV) DestroyIcon(_iconV);
    if (_iconE) DestroyIcon(_iconE);
    _iconV = _iconE = nullptr;
}
static void updateTray(int lang) {
    _nid.hIcon = lang ? _iconV : _iconE;
    wcscpy_s(_nid.szTip, lang ? L"Vikey – Tiếng Việt" : L"Vikey – English");
    if (_trayAdded) Shell_NotifyIconW(NIM_MODIFY, &_nid);
}
static void toggle(const wchar_t* key, int fallback) { vikey::prefs::setInt(key, !vikey::prefs::getInt(key, fallback)); vikey::core::reloadSettings(); }
static std::wstring hotkeyLabel() {
    int hk = vikey::prefs::getInt(L"SwitchKeyStatus", 0); if (!hk) hk = 0x7A000206;
    std::wstring t; if (hk & 0x100) t += L"Ctrl+"; if (hk & 0x200) t += L"Alt+"; if (hk & 0x800) t += L"Shift+"; if (hk & 0x400) t += L"Win+";
    int k = hk & 0xFF; if (k != 0xFE) { unsigned short ch = keyCodeToCharacter((unsigned int)(k | 0x10000)); if (k == 49) t += L"Space"; else if (ch) t += (wchar_t)ch; } else if (!t.empty()) t.pop_back();
    return t;
}
static void showMenu() {
    using namespace vikey;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_GUIDE, L"Hướng dẫn…");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"Giới thiệu Vikey");
    HMENU dbg = CreatePopupMenu();
    AppendMenuW(dbg, MF_STRING | (prefs::getInt(L"TypingLogEnabled", 0) ? MF_CHECKED : 0), ID_LOGTOGGLE, L"Ghi nhật ký gõ");
    AppendMenuW(dbg, MF_STRING | (prefs::getInt(L"TypingLogEnabled", 0) ? 0 : MF_GRAYED), ID_LOG, L"Xem nhật ký gõ…");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)dbg, L"Gỡ lỗi");
    AppendMenuW(m, MF_STRING, ID_QUIT, L"Thoát Vikey");
    POINT pt; GetCursorPos(&pt); SetForegroundWindow(_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, _hwnd, nullptr);
    PostMessageW(_hwnd, WM_NULL, 0, 0);   // or the next menu vanishes at once (TrackPopupMenu remarks)
    DestroyMenu(m);
}
static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    using namespace vikey;
    if (WM_TASKBARCREATED && msg == WM_TASKBARCREATED) {
        _trayAdded = false;
        refreshTrayIcons(true);
        _nid.hIcon = core::language() ? _iconV : _iconE;
        _trayAdded = Shell_NotifyIconW(NIM_ADD, &_nid) != FALSE;
        return 0;
    }   // Explorer restarted: recreate the correctly-sized icon and add it back.
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP) {
            // Handle every click immediately. A double-click may therefore toggle twice,
            // returning the input mode to its original state.
            core::setLanguageByUser(!core::language());
        } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            ui::showSettings(GetModuleHandleW(nullptr), false);
        } else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) showMenu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_ABOUT: ui::showAbout(GetModuleHandleW(nullptr)); break;
        case ID_GUIDE: ui::showOnboarding(GetModuleHandleW(nullptr)); break;
        case ID_LOGTOGGLE: toggle(L"TypingLogEnabled", 0); break;
        case ID_LOG: ui::showTypingLog(GetModuleHandleW(nullptr)); break;
        case ID_QUIT: DestroyWindow(h); break;
        }
        return 0;
    case WM_APP + 3: ui::showSettings(GetModuleHandleW(nullptr), false); return 0;
    case WM_VIKEY_SETLANG: core::setLanguageQuiet((int)wp); return 1;
    case WM_TIMER:
        if (wp == TIMER_FLUSH) core::flushTypingLog();
        else if (wp == TIMER_TRAY_DPI) refreshTrayIcons();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        refreshTrayIcons();
        return 0;
    case site::WM_SITE: { HWND w; std::wstring host = site::lastHost(&w); if (w == GetForegroundWindow()) core::onSiteChanged(host); return 0; }
    case WM_DESTROY:
        core::stop();
        if (_trayAdded) Shell_NotifyIconW(NIM_DELETE, &_nid);
        _trayAdded = false;
        destroyTrayIcons();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    vikey::crash::install();           // a crash leaves a text report (no typed text) for the next launch
    vikey::crash::selfTestIfAsked();   // hidden: --crash-self-test
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\VikeyRunning");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {   // already running: opening the app again shows its window, like on macOS
        HWND t = FindWindowW(L"VikeyTray", nullptr);
        if (t) { DWORD pid = 0; GetWindowThreadProcessId(t, &pid); AllowSetForegroundWindow(pid); PostMessageW(t, WM_APP + 3, 0, 0); }   // our foreground right goes to the running Vikey: its window comes to the front
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput gdipInput;
    if (GdiplusStartup(&_gdipToken, &gdipInput, nullptr) != Ok) {
        MessageBoxW(nullptr, L"Không khởi tạo được bộ vẽ icon.", L"Vikey", MB_ICONERROR);
        return 1;
    }
    vikey::prefs::migrateFromOpenKey();
    // Remove obsolete network-feature state from older installations.
    std::wstring oldUpdate = vikey::prefs::getString(L"PendingUpdatePath", L"");
    if (!oldUpdate.empty()) DeleteFileW(oldUpdate.c_str());
    for (const wchar_t* k : {L"AutoUpdate", L"LastUpdateCheck", L"AppcastURL", L"PendingUpdatePath", L"PendingUpdateVersion", L"CrashReportsAuto", L"CrashReportsSeenUntil", L"crashEndpoint"}) vikey::prefs::remove(k);
    // Removed features: drop their stored prefs, so a stale picker setting cannot come back.
    for (const wchar_t* k : {L"EmojiPicker", L"EmojiHotkey", L"EmojiSkinTone", L"EmojiUsage"}) vikey::prefs::remove(k);
    WNDCLASSW wc = {}; wc.style = CS_DBLCLKS; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyTray";
    RegisterClassW(&wc);
    // hidden top-level, not HWND_MESSAGE: a message-only window gets no broadcasts (TaskbarCreated, WM_SETTINGCHANGE);
    // WS_EX_TOOLWINDOW keeps it out of the taskbar and Alt+Tab
    _hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"VikeyTray", L"Vikey", 0, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!refreshTrayIcons(true)) {
        MessageBoxW(nullptr, L"Không tạo được icon khay hệ thống.", L"Vikey", MB_ICONERROR);
        DestroyWindow(_hwnd);
        GdiplusShutdown(_gdipToken);
        return 1;
    }
    _nid.cbSize = sizeof(_nid); _nid.hWnd = _hwnd; _nid.uID = 1; _nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; _nid.uCallbackMessage = WM_TRAY; _nid.hIcon = _iconV;
    wcscpy_s(_nid.szTip, L"Vikey");
    _trayAdded = Shell_NotifyIconW(NIM_ADD, &_nid) != FALSE;
    vikey::core::setLanguageObserver([](int lang) { updateTray(lang); vikey::ui::languageChanged(lang); });
    if (!vikey::core::start()) {
        MessageBoxW(nullptr, L"Không cài được bộ bắt phím (SetWindowsHookEx).", L"Vikey", MB_ICONERROR);
        if (_trayAdded) Shell_NotifyIconW(NIM_DELETE, &_nid);
        _trayAdded = false;
        DestroyWindow(_hwnd);
        destroyTrayIcons();
        GdiplusShutdown(_gdipToken);
        ReleaseMutex(single); CloseHandle(single);
        return 1;
    }
    updateTray(vikey::core::language());
    if (!vikey::prefs::has(L"FirstRunDone")) {
        vikey::ui::showSettings(inst, true);
    } else if (vikey::prefs::getInt(L"ShowWindowAtStartup", 1) != 0) {
        vikey::ui::showSettings(inst, false);
    }
    SetTimer(_hwnd, TIMER_FLUSH, 10000, nullptr);
    SetTimer(_hwnd, TIMER_TRAY_DPI, 3000, nullptr);
    vikey::site::start(_hwnd);   // per-website memory and password fields, read off the hook thread
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    ReleaseMutex(single); CloseHandle(single);
    if (_gdipToken) GdiplusShutdown(_gdipToken);
    return 0;
}
