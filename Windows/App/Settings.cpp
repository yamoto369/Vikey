// Vikey's windows on Windows, 1:1 with the macOS app (App/Views): the main window (header with
// icon, wordmark, hotkey hint and the Tiếng Việt / English switch, then a grouped form), the
// onboarding (OnboardingView.swift minus the Accessibility step), the About panel, the
// Acknowledgements window and the typing-log window. Drawn with GDI+ so the toggles, segmented
// controls and grouped surfaces follow a lightweight Fluent-inspired Windows visual system.
#define _HAS_STD_BYTE 0
#include "Settings.h"
#include "Core.h"
#include "Prefs.h"
#include "KeyMap.h"
#include "resource.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <dwmapi.h>
#define GDIPVER 0x0110
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <functional>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

namespace vikey { namespace ui {

// ---- shared look ---------------------------------------------------------------------------
static HFONT fBody, fBodyBold, fSmall, fWordmark, fTitle, fMono, fTitle2, fCallout, fMonoBold, fWordmark24;
static const COLORREF BG = RGB(245, 247, 250), GROUP = RGB(255, 255, 255), BORDER = RGB(226, 232, 240), SEP = RGB(237, 241, 246);
static const COLORREF TEXT = RGB(31, 41, 55), SECONDARY = RGB(100, 116, 139), TERTIARY = RGB(148, 163, 184), ACCENT = RGB(37, 99, 235), GREEN = RGB(22, 163, 74), TOGGLE_OFF = RGB(226, 232, 240);
static const COLORREF BRAND = RGB(37, 99, 235);   // Fluent-inspired blue accent
static const int W = 920, MARGIN = 24, CONTENT_X = 220, ROW = 40, GAP = 12, RADIUS = 12;
static ULONG_PTR gdipToken = 0;
static HINSTANCE _inst;
static void initLook() {
    if (fBody) return;
    GdiplusStartupInput in; GdiplusStartup(&gdipToken, &in, nullptr);
    auto mk = [](int px, int weight, const wchar_t* face = L"Segoe UI Variable Text") { return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face); };
    // Segoe UI Variable is available on Windows 11; the font mapper falls back to Segoe UI on older systems.
    fBody = mk(16, FW_NORMAL); fBodyBold = mk(16, FW_SEMIBOLD); fSmall = mk(14, FW_NORMAL); fWordmark = mk(28, FW_BOLD); fTitle = mk(18, FW_SEMIBOLD); fMono = mk(14, FW_NORMAL, L"Consolas");
    fTitle2 = mk(22, FW_SEMIBOLD); fCallout = mk(15, FW_NORMAL); fMonoBold = mk(16, FW_BOLD, L"Consolas"); fWordmark24 = mk(28, FW_BOLD);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS | ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);
}
static Color gp(COLORREF c, BYTE a = 255) { return Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }
static void roundRect(Graphics& g, RectF r, float radius, Color fill, Color stroke, float strokeW = 1) {
    GraphicsPath p; float d = radius * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90); p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90); p.CloseFigure();
    SolidBrush b(fill); g.FillPath(&b, &p);
    if (stroke.GetA()) { Pen pen(stroke, strokeW); g.DrawPath(&pen, &p); }
}
static std::wstring hotkeyDisplay(int hk) {
    std::wstring s;
    if (HAS_CONTROL(hk)) s += L"Ctrl+"; if (HAS_OPTION(hk)) s += L"Alt+"; if (HAS_SHIFT(hk)) s += L"Shift+"; if (HAS_COMMAND(hk)) s += L"Win+";
    int k = GET_SWITCH_KEY(hk);
    if (k != 0xFE) {
        Uint16 ch = keyCodeToCharacter(k | CAPS_MASK);
        if (k == KEY_SPACE) s += L"Space"; else if (k == KEY_RETURN) s += L"Enter"; else if (k == KEY_TAB) s += L"Tab"; else if (ch) s += (wchar_t)ch; else s += L"?";
    } else if (!s.empty()) s.pop_back();
    return s.empty() ? L"—" : s;
}
static int currentHotkey() { int hk = prefs::getInt(L"SwitchKeyStatus", 0); return hk ? hk : 0x7A000206; }
static int textWidth(HDC dc, HFONT f, const std::wstring& s) { SelectObject(dc, f); SIZE sz = {}; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz); return sz.cx; }
static int textHeight(HFONT f, const std::wstring& s, int width) {   // wrapped height
    HDC dc = GetDC(nullptr); SelectObject(dc, f); RECT r = {0, 0, width, 0}; DrawTextW(dc, s.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX); ReleaseDC(nullptr, dc); return r.bottom;
}
/// "Vi" in the text colour, "key" in brand indigo, bold (Brand.swift Wordmark).
static void drawWordmark(HDC dc, HFONT f, int x, int y) {
    SelectObject(dc, f); SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, TEXT); TextOutW(dc, x, y, L"Vi", 2);
    SetTextColor(dc, BRAND); TextOutW(dc, x + textWidth(dc, f, L"Vi"), y, L"key", 3);
}
/// The middle of the screen with the mouse (Mac 1.0.16/1.0.18 centerExactly), inside its work area.
static void centerOnMouseScreen(HWND h) {
    POINT p; GetCursorPos(&p); MONITORINFO mi = {sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    RECT r; GetWindowRect(h, &r); int w = r.right - r.left, hh = r.bottom - r.top; RECT a = mi.rcWork;
    int x = (a.left + a.right - w) / 2, y = (a.top + a.bottom - hh) / 2;
    if (y < a.top) y = a.top; if (x < a.left) x = a.left;
    SetWindowPos(h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
static void bringToFront(HWND h) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); }

// Ask DWM for native Windows 11 rounded window corners and a subtle Fluent border.
static void applyFluentFrame(HWND h) {
    if (!h) return;
    const DWORD rounded = 2; // DWMWCP_ROUND
    const COLORREF border = RGB(226, 232, 240);
    DwmSetWindowAttribute(h, 33, &rounded, sizeof(rounded)); // DWMWA_WINDOW_CORNER_PREFERENCE
    DwmSetWindowAttribute(h, 34, &border, sizeof(border));   // DWMWA_BORDER_COLOR
}

// Lines of text with links, drawn by hand so they can be centred (About) and hit-tested.
struct Seg { std::wstring text; std::wstring url; HFONT font; COLORREF color; };
struct Link { RECT r; std::wstring url; };
static int drawSegs(HDC dc, const std::vector<Seg>& segs, int x, int y, int width, bool center, std::vector<Link>& links) {
    int total = 0, h = 0;
    for (auto& s : segs) { total += textWidth(dc, s.font, s.text); TEXTMETRICW tm; GetTextMetricsW(dc, &tm); h = std::max(h, (int)tm.tmHeight); }
    int cx = center ? x + (width - total) / 2 : x;
    SetBkMode(dc, TRANSPARENT);
    for (auto& s : segs) {
        int w = textWidth(dc, s.font, s.text); SetTextColor(dc, s.url.empty() ? s.color : ACCENT);
        TextOutW(dc, cx, y, s.text.c_str(), (int)s.text.size());
        if (!s.url.empty()) links.push_back({{cx, y, cx + w, y + h}, s.url});
        cx += w;
    }
    return h;
}
static const Link* linkAt(const std::vector<Link>& links, LPARAM lp) { POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; for (auto& l : links) if (PtInRect(&l.r, p)) return &l; return nullptr; }

// ---- custom controls -----------------------------------------------------------------------
// The background a control sits on: white in groups and onboarding, gray on the window.
static COLORREF bgOf(HWND h) { UINT_PTR v = (UINT_PTR)GetPropW(h, L"vkbg"); return v ? (COLORREF)(v - 1) : GROUP; }
static void setBg(HWND h, COLORREF c) { SetPropW(h, L"vkbg", (HANDLE)(UINT_PTR)(c + 1)); }

// Toggle switch: GWLP_USERDATA = on/off; BN_CLICKED to the parent on change; disabled = grayed.
static LRESULT CALLBACK toggleProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(bgOf(h)); FillRect(mem, &rc, bg); DeleteObject(bg);
        Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool on = GetWindowLongPtrW(h, GWLP_USERDATA) != 0, enabled = IsWindowEnabled(h);
        Color fill = on ? gp(ACCENT, enabled ? 255 : 110) : gp(TOGGLE_OFF);
        roundRect(g, RectF(0.5f, 0.5f, 39, 22), 11, fill, Color(0, 0, 0, 0));
        SolidBrush knob(Color(255, 255, 255, 255)); float kx = on ? 19.5f : 2.5f;
        SolidBrush shadow(Color(40, 0, 0, 0)); g.FillEllipse(&shadow, kx, 3.0f, 18.0f, 18.0f);
        g.FillEllipse(&knob, kx, 2.5f, 18.0f, 18.0f);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0; }
    case WM_LBUTTONUP: case WM_KEYUP:
        if (m == WM_KEYUP && w != VK_SPACE) break;
        if (!IsWindowEnabled(h)) return 0;
        SetWindowLongPtrW(h, GWLP_USERDATA, !GetWindowLongPtrW(h, GWLP_USERDATA)); InvalidateRect(h, nullptr, FALSE);
        SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_ENABLE: InvalidateRect(h, nullptr, FALSE); break;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE;
    case WM_NCDESTROY: RemovePropW(h, L"vkbg"); break;
    }
    return DefWindowProcW(h, m, w, l);
}
static bool toggleOn(HWND h) { return GetWindowLongPtrW(h, GWLP_USERDATA) != 0; }
static void setToggle(HWND h, bool on) { SetWindowLongPtrW(h, GWLP_USERDATA, on); InvalidateRect(h, nullptr, FALSE); }

// Segmented control with two segments; GWLP_USERDATA = selected index; BN_CLICKED on change.
static LRESULT CALLBACK segProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static const wchar_t* labels[2] = {L"Tiếng Việt", L"English"};
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(BG); FillRect(mem, &rc, bg); DeleteObject(bg);
        Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias);
        roundRect(g, RectF(0.5f, 0.5f, (float)rc.right - 1, (float)rc.bottom - 1), 8, gp(TOGGLE_OFF), Color(0, 0, 0, 0));
        int sel = (int)GetWindowLongPtrW(h, GWLP_USERDATA); float half = rc.right / 2.0f;
        roundRect(g, RectF(sel * half + 2, 2, half - 4, (float)rc.bottom - 4), 7, gp(ACCENT), Color(0, 0, 0, 0));
        SelectObject(mem, fBody); SetBkMode(mem, TRANSPARENT);
        for (int i = 0; i < 2; i++) { RECT r = {(LONG)(i * half), 0, (LONG)((i + 1) * half), rc.bottom}; SetTextColor(mem, i == sel ? RGB(255, 255, 255) : TEXT); DrawTextW(mem, labels[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0; }
    case WM_LBUTTONUP: { RECT rc; GetClientRect(h, &rc); int i = GET_X_LPARAM(l) < rc.right / 2 ? 0 : 1;
        if (i != (int)GetWindowLongPtrW(h, GWLP_USERDATA)) { SetWindowLongPtrW(h, GWLP_USERDATA, i); InvalidateRect(h, nullptr, FALSE); SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h); }
        return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
}

// Fluent button: GWLP_USERDATA = prominent/selected. Supports hover, pressed and keyboard focus.
static LRESULT CALLBACK buttonProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(bgOf(h)); FillRect(mem, &rc, bg); DeleteObject(bg);
        Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool selected = GetWindowLongPtrW(h, GWLP_USERDATA) != 0, enabled = IsWindowEnabled(h);
        bool down = GetPropW(h, L"vkdown") != nullptr, hover = GetPropW(h, L"vkhover") != nullptr;
        bool focused = GetFocus() == h;
        Color fill = selected ? gp(ACCENT, enabled ? (down ? 220 : 255) : 100) :
            gp(down ? RGB(218, 226, 240) : (hover ? RGB(239, 243, 250) : RGB(243, 245, 249)));
        Color border = selected ? Color(0, 0, 0, 0) : gp(hover ? RGB(190, 204, 225) : BORDER);
        roundRect(g, RectF(0.5f, 0.5f, (float)rc.right - 1, (float)rc.bottom - 1), 8, fill, border);
        if (focused) {
            Pen focus(gp(ACCENT, 210), 1.4f);
            g.DrawRectangle(&focus, 2.0f, 2.0f, (float)rc.right - 5.0f, (float)rc.bottom - 5.0f);
        }
        wchar_t t[96] = {}; GetWindowTextW(h, t, 96);
        SelectObject(mem, fBody); SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, selected ? RGB(255, 255, 255) : (enabled ? TEXT : TERTIARY));
        RECT tr = rc; InflateRect(&tr, -8, 0);
        DrawTextW(mem, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0; }
    case WM_MOUSEMOVE: {
        if (!GetPropW(h, L"vkhover")) {
            SetPropW(h, L"vkhover", (HANDLE)1);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0}; TrackMouseEvent(&tme);
            InvalidateRect(h, nullptr, FALSE);
        }
        break; }
    case WM_MOUSELEAVE: RemovePropW(h, L"vkhover"); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(h)) { SetPropW(h, L"vkdown", (HANDLE)1); SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP: {
        bool was = GetPropW(h, L"vkdown") != nullptr; RemovePropW(h, L"vkdown");
        if (GetCapture() == h) ReleaseCapture();
        InvalidateRect(h, nullptr, FALSE);
        RECT rc; GetClientRect(h, &rc); POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (was && PtInRect(&rc, p) && IsWindowEnabled(h)) SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h);
        return 0; }
    case WM_KEYUP:
        if ((w == VK_SPACE || w == VK_RETURN) && IsWindowEnabled(h))
            SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h);
        return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE:
        InvalidateRect(h, nullptr, FALSE); break;
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IsWindowEnabled(h) ? IDC_HAND : IDC_ARROW)); return TRUE;
    case WM_NCDESTROY:
        RemovePropW(h, L"vkbg"); RemovePropW(h, L"vkdown"); RemovePropW(h, L"vkhover"); break;
    }
    return DefWindowProcW(h, m, w, l);
}

// Fluent ComboBox: fully custom field and popup; no native COMBOBOX chrome/chevron.
struct ComboData { std::vector<std::wstring> items; int selected = 0; HWND popup = nullptr; };
static HWND _comboPopup = nullptr;
static int _comboHover = -1;
static constexpr int COMBO_ITEM_H = 34;

static void closeComboPopup() {
    if (_comboPopup && IsWindow(_comboPopup)) DestroyWindow(_comboPopup);
    _comboPopup = nullptr; _comboHover = -1;
}
static void chooseComboItem(HWND combo, int index) {
    auto* d = (ComboData*)GetWindowLongPtrW(combo, GWLP_USERDATA);
    if (!d || index < 0 || index >= (int)d->items.size()) return;
    d->selected = index;
    InvalidateRect(combo, nullptr, FALSE);
    HWND parent = GetParent(combo);
    closeComboPopup();
    if (IsWindow(combo)) SetFocus(combo);
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE), (LPARAM)combo);
}
static LRESULT CALLBACK comboPopupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    HWND combo = (HWND)GetWindowLongPtrW(h, GWLP_USERDATA);
    auto* d = combo ? (ComboData*)GetWindowLongPtrW(combo, GWLP_USERDATA) : nullptr;
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp); HBRUSH bg = CreateSolidBrush(GROUP); FillRect(mem, &rc, bg); DeleteObject(bg);
        Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias);
        roundRect(g, RectF(0.5f, 0.5f, (float)rc.right - 1, (float)rc.bottom - 1), 9, gp(GROUP), gp(BORDER));
        if (d) for (int i = 0; i < (int)d->items.size(); ++i) {
            RECT item = {5, 4 + i * COMBO_ITEM_H, rc.right - 5, 4 + (i + 1) * COMBO_ITEM_H};
            if (i == _comboHover || i == d->selected) {
                roundRect(g, RectF((float)item.left, (float)item.top, (float)(item.right-item.left), (float)(item.bottom-item.top)), 6,
                    gp(i == _comboHover ? RGB(239, 244, 255) : RGB(247, 249, 252)), Color(0,0,0,0));
            }
            SelectObject(mem, fBody); SetBkMode(mem, TRANSPARENT); SetTextColor(mem, i == d->selected ? ACCENT : TEXT);
            RECT tr = item; tr.left += 12; DrawTextW(mem, d->items[i].c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (i == d->selected) { SetTextColor(mem, ACCENT); RECT mark = item; mark.right -= 12; DrawTextW(mem, L"✓", -1, &mark, DT_RIGHT | DT_VCENTER | DT_SINGLELINE); }
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t);
        int i = (GET_Y_LPARAM(l) - 4) / COMBO_ITEM_H;
        int next = d && i >= 0 && i < (int)d->items.size() ? i : -1;
        if (next != _comboHover) { _comboHover = next; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: _comboHover = -1; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: {
        int i = (GET_Y_LPARAM(l) - 4) / COMBO_ITEM_H;
        if (d && i >= 0 && i < (int)d->items.size()) chooseComboItem(combo, i);
        else closeComboPopup();
        return 0;
    }
    case WM_KEYDOWN:
        if (d && (w == VK_DOWN || w == VK_UP)) {
            int n = (int)d->items.size(); if (n) { _comboHover = (_comboHover < 0 ? d->selected : _comboHover);
                _comboHover = (w == VK_DOWN) ? std::min(n - 1, _comboHover + 1) : std::max(0, _comboHover - 1); InvalidateRect(h, nullptr, FALSE); }
            return 0;
        }
        if (w == VK_RETURN && _comboHover >= 0) { chooseComboItem(combo, _comboHover); return 0; }
        if (w == VK_ESCAPE) { closeComboPopup(); SetFocus(combo); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) { _comboPopup = nullptr; _comboHover = -1; if (d) d->popup = nullptr; DestroyWindow(h); return 0; }
        break;
    case WM_NCDESTROY:
        if (_comboPopup == h) _comboPopup = nullptr;
        if (d) d->popup = nullptr;
        break;
    }
    return DefWindowProcW(h, m, w, l);
}
static void openComboPopup(HWND combo) {
    auto* d = (ComboData*)GetWindowLongPtrW(combo, GWLP_USERDATA);
    if (!d || d->items.empty()) return;
    if (_comboPopup) { closeComboPopup(); }
    RECT r; GetWindowRect(combo, &r);
    HMONITOR mon = MonitorFromWindow(combo, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)}; GetMonitorInfoW(mon, &mi);
    int popupWidth = r.right - r.left;
    int workHeight = mi.rcWork.bottom - mi.rcWork.top - 8;
    int height = std::min((int)d->items.size() * COMBO_ITEM_H + 8, workHeight);
    int y = r.bottom + 2;
    if (y + height > mi.rcWork.bottom - 4) {
        y = r.top - height;
        if (y < mi.rcWork.top + 4) y = mi.rcWork.bottom - height - 4;
    }
    int x = std::max(mi.rcWork.left + 4, std::min(r.left, mi.rcWork.right - popupWidth - 4));
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"VikeyComboPopup", L"",
        WS_POPUP, x, y, popupWidth, height, GetParent(combo), nullptr, _inst, nullptr);
    if (!popup) return;
    SetWindowLongPtrW(popup, GWLP_USERDATA, (LONG_PTR)combo);
    d->popup = popup; _comboPopup = popup; _comboHover = d->selected;
    ShowWindow(popup, SW_SHOWNOACTIVATE); SetForegroundWindow(popup); SetFocus(popup);
}
static LRESULT CALLBACK comboProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* d = (ComboData*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)new ComboData()); return 0;
    case CB_ADDSTRING:
        if (d) { d->items.push_back((const wchar_t*)l); return (LRESULT)d->items.size()-1; } return CB_ERR;
    case CB_SETCURSEL:
        if (d && (int)w >= 0 && (int)w < (int)d->items.size()) { d->selected = (int)w; InvalidateRect(h, nullptr, FALSE); return d->selected; } return CB_ERR;
    case CB_GETCURSEL: return d ? d->selected : CB_ERR;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp); HBRUSH bg = CreateSolidBrush(bgOf(h)); FillRect(mem, &rc, bg); DeleteObject(bg);
        Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool hover = GetPropW(h, L"vkhover") != nullptr, focused = GetFocus() == h;
        roundRect(g, RectF(0.5f,0.5f,(float)rc.right-1,(float)rc.bottom-1), 7, gp(hover ? RGB(250,252,255) : GROUP), gp(focused ? ACCENT : (hover ? RGB(190,204,225) : BORDER)), focused ? 1.5f : 1.0f);
        SelectObject(mem, fBody); SetBkMode(mem, TRANSPARENT); SetTextColor(mem, TEXT);
        RECT tr = rc; tr.left += 12; tr.right -= 34;
        if (d && d->selected >= 0 && d->selected < (int)d->items.size()) DrawTextW(mem, d->items[d->selected].c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        // One custom chevron, rendered as a simple stroked V.
        Pen arrow(gp(SECONDARY), 1.6f); REAL cx = (REAL)rc.right - 18, cy = (REAL)rc.bottom / 2.0f;
        g.DrawLine(&arrow, cx - 4, cy - 2, cx, cy + 2); g.DrawLine(&arrow, cx, cy + 2, cx + 4, cy - 2);
        BitBlt(dc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY); SelectObject(mem,old); DeleteObject(bmp); DeleteDC(mem); EndPaint(h,&ps); return 0;
    }
    case WM_MOUSEMOVE:
        if (!GetPropW(h,L"vkhover")) { SetPropW(h,L"vkhover",(HANDLE)1); TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,h,0}; TrackMouseEvent(&t); InvalidateRect(h,nullptr,FALSE); } return 0;
    case WM_MOUSELEAVE: RemovePropW(h,L"vkhover"); InvalidateRect(h,nullptr,FALSE); return 0;
    case WM_LBUTTONUP: openComboPopup(h); return 0;
    case WM_KEYDOWN:
        if (w == VK_SPACE || w == VK_RETURN || w == VK_DOWN) { openComboPopup(h); return 0; }
        if (d && (w == VK_UP || w == VK_LEFT || w == VK_RIGHT)) {
            int n = (int)d->items.size(); if (n) { d->selected = std::max(0, std::min(n-1, d->selected + ((w == VK_UP || w == VK_LEFT) ? -1 : 1))); InvalidateRect(h,nullptr,FALSE); SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(h),CBN_SELCHANGE),(LPARAM)h); } return 0;
        }
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(h,nullptr,FALSE); break;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr,IDC_HAND)); return TRUE;
    case WM_NCDESTROY: if (d) delete d; RemovePropW(h,L"vkhover"); break;
    }
    return DefWindowProcW(h,m,w,l);
}

// Hotkey recorder (HotkeyRecorder.swift): a rounded gray box; click it, press a combination. The switch hotkey
// also takes modifiers alone (Ctrl+Shift).
struct Recorder { const wchar_t* pref; bool recording; int value; };
static Recorder* rec(HWND h) { return (Recorder*)GetWindowLongPtrW(h, GWLP_USERDATA); }
static void afterHotkeyChange();
static void commitRecorder(HWND h, int value) {
    Recorder* r = rec(h);
    r->value = value | (currentHotkey() & 0x8000);
    r->recording = false; prefs::setInt(r->pref, r->value); core::reloadSettings(); InvalidateRect(h, nullptr, FALSE);
    SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h);
    afterHotkeyChange();
}
static bool isModVk(DWORD vk) { return vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LSHIFT || vk == VK_RSHIFT; }
static LRESULT CALLBACK hotkeyProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Recorder* r = rec(h);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(bgOf(h)); FillRect(mem, &rc, bg); DeleteObject(bg);
        Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool recording = r && r->recording;
        roundRect(g, RectF(0.5f, 0.5f, (float)rc.right - 1, (float)rc.bottom - 1), 7, gp(RGB(230, 230, 230)), recording ? gp(ACCENT) : Color(0, 0, 0, 0), 1.5f);
        SelectObject(mem, fBody); SetBkMode(mem, TRANSPARENT); SetTextColor(mem, recording ? SECONDARY : TEXT);
        std::wstring t = recording ? L"Bấm phím tắt…" : (r && r->value == 0) ? L"Chưa đặt" : hotkeyDisplay(r ? r->value : 0);
        DrawTextW(mem, t.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0; }
    case WM_LBUTTONUP: if (r) { if (r->recording) { r->recording = false; } else { SetFocus(h); r->recording = true; } InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_KILLFOCUS: if (r) r->recording = false; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_CHAR: case WM_SYSCHAR: return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        if (!r || !r->recording) return 0;
        DWORD vk = (DWORD)w;
        bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0, shift = GetKeyState(VK_SHIFT) < 0, win = GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0;
        if (vk == VK_ESCAPE && !ctrl && !alt && !win && !shift) { r->recording = false; InvalidateRect(h, nullptr, FALSE); return 0; }   // Esc cancels
        if (isModVk(vk)) return 0;   // modifiers alone: committed on key-up
        int key = (int)engineKey(vk); if (key == KEY_EMPTY) return 0;
        if (!(ctrl || alt || win || shift)) return 0;   // a key alone is not a shortcut
        commitRecorder(h, key | (ctrl ? 0x100 : 0) | (alt ? 0x200 : 0) | (win ? 0x400 : 0) | (shift ? 0x800 : 0));
        return 0; }
    case WM_KEYUP: case WM_SYSKEYUP: {
        if (!r || !r->recording) return 0;
        DWORD vk = (DWORD)w; if (!isModVk(vk)) return 0;
        // modifiers only: the combination that was held when the first one is released, at least two of them
        int mods = (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL || GetKeyState(VK_CONTROL) < 0 ? 0x100 : 0) | (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU || GetKeyState(VK_MENU) < 0 ? 0x200 : 0) |
                   (vk == VK_LWIN || vk == VK_RWIN || GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0 ? 0x400 : 0) | (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT || GetKeyState(VK_SHIFT) < 0 ? 0x800 : 0);
        int n = !!(mods & 0x100) + !!(mods & 0x200) + !!(mods & 0x400) + !!(mods & 0x800);
        if (n < 2) return 0;
        commitRecorder(h, 0xFE | mods); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE;
    case WM_NCDESTROY: delete r; SetWindowLongPtrW(h, GWLP_USERDATA, 0); RemovePropW(h, L"vkbg"); break;
    }
    return DefWindowProcW(h, m, w, l);
}

// (?) tip: a small circle with a question mark; hover shows the tip, click shows it as a popup.
static LRESULT CALLBACK tipProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(GROUP); FillRect(dc, &rc, bg); DeleteObject(bg);
        Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias); Pen p(gp(RGB(170, 170, 175)), 1.2f); g.DrawEllipse(&p, 1.5f, 1.5f, 13.0f, 13.0f);
        SelectObject(dc, fSmall); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(150, 150, 155)); RECT r = {0, -1, 16, 16}; DrawTextW(dc, L"?", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps); return 0; }
    case WM_LBUTTONUP: { wchar_t t[512]; GetWindowTextW(h, t, 512); MessageBoxW(GetParent(h), t, L"Vikey", MB_OK | MB_ICONINFORMATION); return 0; }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h, m, w, l);
}

static void registerClasses(HINSTANCE inst) {
    static bool done = false; if (done) return; done = true;
    struct { const wchar_t* name; WNDPROC proc; } cls[] = {{L"VikeyToggle", toggleProc}, {L"VikeySegmented", segProc}, {L"VikeyHotkey", hotkeyProc}, {L"VikeyTip", tipProc}, {L"VikeyButton", buttonProc}, {L"VikeyCombo", comboProc}, {L"VikeyComboPopup", comboPopupProc}};
    for (auto& c : cls) { WNDCLASSW wc = {}; wc.lpfnWndProc = c.proc; wc.hInstance = inst; wc.lpszClassName = c.name; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc); }
}
static HWND makeChild(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f = nullptr) {
    HWND c = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (f) SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return c;
}
static HWND makeRecorder(HWND parent, const wchar_t* pref, int value, int x, int y, int w, int h, int id, COLORREF bg) {
    HWND c = makeChild(parent, L"VikeyHotkey", L"", WS_TABSTOP, x, y, w, h, id); setBg(c, bg);
    SetWindowLongPtrW(c, GWLP_USERDATA, (LONG_PTR)new Recorder{pref, false, value});
    return c;
}
static void destroyChildren(HWND h) { std::vector<HWND> kids; for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) kids.push_back(c); for (HWND c : kids) DestroyWindow(c); }

// ---- main window -----------------------------------------------------------------------------
enum { C_SEG = 200, C_HOTKEY, C_BEEP, C_AUTO, C_PAUSE, C_SMART, C_SITE, C_MARK, C_SPELL, C_RESTORE, C_LOGIN, C_UPDATE, C_LOGKEYS, C_STYLE, C_BLACKLIST,
       L_REFRESH = 300, L_OPEN, L_CLEAR, A_LINK = 400, O_BACK = 500, O_NEXT, O_REC, O_BEEP, O_EDIT = 520,
       C_NAV_GENERAL = 600, C_NAV_TYPING, C_NAV_ADVANCED };
static const UINT WM_REBUILD = WM_APP + 20;
static HWND _win, _seg, _hint, _logWin, _logList, _logSummary, _logGrid, _aboutWin, _onbWin, _ackWin, _tooltip;
static int _settingsPage = 0;
static std::map<int, HWND> _ctl;
struct Group { int top, bottom; }; static std::vector<Group> _groups; static std::vector<std::pair<int, std::wstring>> _titles;   // y, title
static std::vector<int> _seps;

static HWND child(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f = nullptr) { return makeChild(_win, cls, text, style, x, y, w, h, id, f); }
static void addTip(HWND tip, const wchar_t* text) {
    if (!_tooltip) { _tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, _win, nullptr, nullptr, nullptr); SendMessageW(_tooltip, TTM_SETMAXTIPWIDTH, 0, 300); }
    TOOLINFOW ti = {sizeof(ti)}; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = _win; ti.uId = (UINT_PTR)tip; ti.lpszText = (LPWSTR)text; SendMessageW(_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}
/// One form row: label (+ tip) on the left, a control on the right. Returns the control.
static int _y; static int _groupTop = -1;
static void beginGroup(const wchar_t* title) { if (title) { _titles.push_back({_y, title}); _y += 28; } _groupTop = _y; }
static void endGroup() { _groups.push_back({_groupTop, _y}); _y += GAP; }
static void rowLabel(const wchar_t* label, const wchar_t* tip) {
    if (_y != _groupTop) _seps.push_back(_y);
    int ly = _y + (ROW - 24) / 2;
    child(L"STATIC", label, SS_LEFT | SS_NOPREFIX, CONTENT_X + 18, ly, 360, 24, 0, fBody);
    HDC dc = GetDC(_win); int lw = textWidth(dc, fBody, label); ReleaseDC(_win, dc);
    if (tip) { HWND t = child(L"VikeyTip", tip, 0, CONTENT_X + 18 + lw + 8, ly + 4, 16, 16, 0); addTip(t, tip); }
}
static HWND row(const wchar_t* label, const wchar_t* tip, const wchar_t* cls, int id, int cw, int ch, DWORD style = 0) {
    rowLabel(label, tip);
    HWND c = child(cls, L"", style, W - MARGIN - 18 - cw, _y + (ROW - ch) / 2, cw, ch, id, fBody);
    _ctl[id] = c; _y += ROW; return c;
}
static HWND toggleRow(const wchar_t* label, const wchar_t* tip, int id, bool on) { HWND c = row(label, tip, L"VikeyToggle", id, 40, 23); setToggle(c, on); return c; }
static HWND recorderRow(const wchar_t* label, const wchar_t* tip, const wchar_t* pref, int value, int id) {
    rowLabel(label, tip);
    HWND c = makeRecorder(_win, pref, value, W - MARGIN - 18 - 170, _y + (ROW - 32) / 2, 170, 32, id, GROUP);
    _ctl[id] = c; _y += ROW; return c;
}

static void refreshEnabled() {
    EnableWindow(_ctl[C_PAUSE], toggleOn(_ctl[C_AUTO]));
    EnableWindow(_ctl[C_SITE], toggleOn(_ctl[C_SMART]));
    EnableWindow(_ctl[C_RESTORE], toggleOn(_ctl[C_SPELL]));
}
static void setHint() { if (_hint) SetWindowTextW(_hint, (L"Bấm " + hotkeyDisplay(currentHotkey()) + L" để chuyển").c_str()); }

static void paintMain(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    HBRUSH bg = CreateSolidBrush(BG); FillRect(dc, &rc, bg); DeleteObject(bg);
    Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
    // Persistent navigation rail and separate content canvas.
    roundRect(g, RectF(8, 8, 194, (float)rc.bottom - 16), 14, gp(GROUP), gp(BORDER));
    roundRect(g, RectF((float)CONTENT_X, 8, (float)(W - CONTENT_X - MARGIN), (float)rc.bottom - 16), 14, gp(BG), Color(0, 0, 0, 0));
    for (auto& gr : _groups) roundRect(g, RectF((float)CONTENT_X, (float)gr.top, (float)(W - CONTENT_X - MARGIN), (float)(gr.bottom - gr.top)), RADIUS, gp(GROUP), gp(BORDER));
    Pen sep(gp(SEP), 1); for (int y : _seps) g.DrawLine(&sep, CONTENT_X + 18, y, W - MARGIN - 18, y);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, TEXT);
    SelectObject(dc, fTitle); for (auto& t : _titles) { RECT r = {CONTENT_X + 18, t.first + 2, W - MARGIN, t.first + 28}; SetTextColor(dc, BRAND); DrawTextW(dc, t.second.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX); }
    // Brand and page header.
    drawWordmark(dc, fWordmark, 24, 22);
    SelectObject(dc, fSmall); SetTextColor(dc, SECONDARY);
    RECT navCaption = {24, 68, 180, 92}; DrawTextW(dc, L"CÀI ĐẶT", -1, &navCaption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, fTitle2); SetTextColor(dc, TEXT);
    const wchar_t* pageTitle = _settingsPage == 0 ? L"Tổng quan" : (_settingsPage == 1 ? L"Gõ tiếng Việt" : L"Nâng cao");
    RECT titleRect = {CONTENT_X + 18, 22, W - MARGIN - 290, 54}; DrawTextW(dc, pageTitle, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, fSmall); SetTextColor(dc, SECONDARY);
    const wchar_t* subtitle = _settingsPage == 0 ? L"Thiết lập cách chuyển đổi và hành vi nhập liệu." :
        (_settingsPage == 1 ? L"Tùy chỉnh kiểu gõ và quy tắc tiếng Việt." : L"Quản lý ghi nhớ, khởi động và ứng dụng ngoại lệ.");
    RECT subRect = {CONTENT_X + 19, 54, W - MARGIN - 220, 76}; DrawTextW(dc, subtitle, -1, &subRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    EndPaint(h, &ps);
}

static void buildMain() {
    closeComboPopup();
    destroyChildren(_win);
    if (_tooltip) { DestroyWindow(_tooltip); _tooltip = nullptr; }
    _groups.clear(); _titles.clear(); _seps.clear(); _ctl.clear();

    // Sidebar navigation uses the same custom Fluent button primitive as the rest of the app.
    auto nav = [&](const wchar_t* label, int id, int page, int y) {
        HWND b = makeChild(_win, L"VikeyButton", label, WS_TABSTOP, 20, y, 170, 40, id);
        SetWindowLongPtrW(b, GWLP_USERDATA, page == _settingsPage ? 1 : 0);
        setBg(b, GROUP);
    };
    nav(L"Tổng quan", C_NAV_GENERAL, 0, 104);
    nav(L"Gõ tiếng Việt", C_NAV_TYPING, 1, 152);
    nav(L"Nâng cao", C_NAV_ADVANCED, 2, 200);

    _hint = child(L"STATIC", L"", SS_LEFT, 0, 0, 1, 1, 0, fSmall);
    _seg = child(L"VikeySegmented", L"", 0, W - MARGIN - 164 - 112, 24, 164, 32, C_SEG);
    languageChanged(core::language());
    _y = 92;
    if (_settingsPage == 0) {
        beginGroup(L"Chuyển đổi");
        recorderRow(L"Phím chuyển Anh/Việt", L"Bấm tổ hợp phím này để chuyển giữa tiếng Việt và tiếng Anh. Bấm vào ô để đặt phím khác.", L"SwitchKeyStatus", currentHotkey(), C_HOTKEY);
        toggleRow(L"Âm báo khi chuyển", L"Phát một âm ngắn mỗi lần bạn bấm phím chuyển Anh/Việt.", C_BEEP, HAS_BEEP(currentHotkey()) != 0);
        endGroup();
        beginGroup(L"Tự động");
        toggleRow(L"Tự chuyển Anh/Việt", L"Tự nhận biết và chuyển chế độ khi bạn bắt đầu gõ ngôn ngữ còn lại.", C_AUTO, prefs::getInt(L"AutoSwitchByTyping", 1) != 0);
        toggleRow(L"Tạm dừng 5 giây", L"Khi bạn tự bấm chuyển Anh/Việt, Vikey tạm dừng tự chuyển trong 5 giây.", C_PAUSE, prefs::getInt(L"PauseAutoAfterManual", 1) != 0);
        endGroup();
    } else if (_settingsPage == 1) {
        beginGroup(L"Kiểu gõ");
        HWND style = row(L"Kiểu gõ", L"Chọn biến thể Telex phù hợp với thói quen của bạn.", L"VikeyCombo", C_STYLE, 210, 36, WS_TABSTOP);
        for (const wchar_t* t : {L"Telex", L"Telex đơn giản 1", L"Telex đơn giản 2"}) SendMessageW(style, CB_ADDSTRING, 0, (LPARAM)t);
        SendMessageW(style, CB_SETCURSEL, std::min(2, std::max(0, prefs::getInt(L"TelexStyle", 0))), 0);
        SetWindowPos(style, nullptr, W - MARGIN - 18 - 210, _y - ROW + 2, 210, 36, SWP_NOZORDER);
        HWND mark = row(L"Đặt dấu", L"Chọn vị trí dấu thanh cho các tổ hợp oa, oe và uy.", L"VikeyCombo", C_MARK, 180, 36, WS_TABSTOP);
        SendMessageW(mark, CB_ADDSTRING, 0, (LPARAM)L"hoà, thuý"); SendMessageW(mark, CB_ADDSTRING, 0, (LPARAM)L"hòa, thúy");
        SendMessageW(mark, CB_SETCURSEL, prefs::getInt(L"ModernOrthography", 1) ? 0 : 1, 0);
        SetWindowPos(mark, nullptr, W - MARGIN - 18 - 180, _y - ROW + 2, 180, 36, SWP_NOZORDER);
        endGroup();
        beginGroup(L"Kiểm tra");
        toggleRow(L"Kiểm tra chính tả", L"Chỉ bỏ dấu khi từ đang gõ đúng quy tắc chính tả tiếng Việt.", C_SPELL, prefs::getInt(L"Spelling", 1) != 0);
        toggleRow(L"Trả lại phím khi sai", L"Khi từ sai chính tả, Vikey trả lại đúng các phím bạn đã gõ.", C_RESTORE, prefs::getInt(L"RestoreIfInvalidWord", 1) != 0);
        endGroup();
    } else {
        beginGroup(L"Ghi nhớ");
        toggleRow(L"Nhớ theo ứng dụng", L"Nhớ chế độ tiếng Anh hoặc tiếng Việt riêng cho từng ứng dụng.", C_SMART, prefs::getInt(L"UseSmartSwitchKey", 1) != 0);
        toggleRow(L"Nhớ theo trang web", L"Nhớ chế độ nhập liệu cho từng trang web trong trình duyệt được hỗ trợ.", C_SITE, prefs::getInt(L"SmartSwitchPerSite", 1) != 0);
        endGroup();
        beginGroup(L"Hệ thống");
        toggleRow(L"Mở Vikey khi đăng nhập", L"Tự mở Vikey mỗi khi bạn đăng nhập vào Windows.", C_LOGIN, prefs::launchAtLogin());
        toggleRow(L"Tự động cập nhật", L"Kiểm tra và cài đặt phiên bản mới khi có bản cập nhật.", C_UPDATE, prefs::getInt(L"AutoUpdate", 1) != 0);
        HWND bl = row(L"Ứng dụng ngoại lệ", L"Chọn ứng dụng mà Vikey sẽ không can thiệp khi bạn gõ.", L"VikeyButton", C_BLACKLIST, 132, 32, WS_TABSTOP);
        SetWindowTextW(bl, L"Quản lý…"); setBg(bl, GROUP);
        endGroup();
    }
    refreshEnabled();
    RECT wr = {0, 0, W, 620};
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    SetWindowPos(_win, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RedrawWindow(_win, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
static LRESULT CALLBACK mainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: paintMain(h); return 0;
    case WM_CTLCOLORSTATIC: { static HBRUSH bgBrush = CreateSolidBrush(BG), groupBrush = CreateSolidBrush(GROUP); SetBkMode((HDC)wp, TRANSPARENT); HWND c = (HWND)lp; SetTextColor((HDC)wp, c == _hint ? SECONDARY : TEXT); return (LRESULT)(c == _hint ? bgBrush : groupBrush); }
    case WM_COMMAND: {
        int id = LOWORD(wp); HWND c = (HWND)lp;
        auto flag = [&](const wchar_t* pref) { prefs::setInt(pref, toggleOn(c)); core::reloadSettings(); refreshEnabled(); };
        switch (id) {
        case C_NAV_GENERAL: _settingsPage = 0; buildMain(); break;
        case C_NAV_TYPING: _settingsPage = 1; buildMain(); break;
        case C_NAV_ADVANCED: _settingsPage = 2; buildMain(); break;
        case C_SEG: core::setLanguageByUser(GetWindowLongPtrW(_seg, GWLP_USERDATA) == 0 ? 1 : 0); break;
        case C_HOTKEY: setHint(); break;
        case C_BEEP: { int hk = (currentHotkey() & ~0x8000) | (toggleOn(c) ? 0x8000 : 0); prefs::setInt(L"SwitchKeyStatus", hk); if (Recorder* r = rec(_ctl[C_HOTKEY])) r->value = hk; core::reloadSettings(); afterHotkeyChange(); break; }
        case C_AUTO: flag(L"AutoSwitchByTyping"); break;
        case C_PAUSE: flag(L"PauseAutoAfterManual"); break;
        case C_SMART: flag(L"UseSmartSwitchKey"); break;
        case C_SITE: flag(L"SmartSwitchPerSite"); break;
        case C_STYLE: if (HIWORD(wp) == CBN_SELCHANGE) { prefs::setInt(L"TelexStyle", (int)SendMessageW(c, CB_GETCURSEL, 0, 0)); core::reloadSettings(); } break;
        case C_MARK: if (HIWORD(wp) == CBN_SELCHANGE) { prefs::setInt(L"ModernOrthography", SendMessageW(c, CB_GETCURSEL, 0, 0) == 0 ? 1 : 0); core::reloadSettings(); } break;
        case C_SPELL: flag(L"Spelling"); break;
        case C_RESTORE: flag(L"RestoreIfInvalidWord"); break;
        case C_LOGIN: prefs::setLaunchAtLogin(toggleOn(c)); setToggle(c, prefs::launchAtLogin()); break;
        case C_UPDATE: prefs::setInt(L"AutoUpdate", toggleOn(c)); break;
        case C_BLACKLIST: showBlacklist(_inst); break;
        }
        return 0; }
    case WM_REBUILD: buildMain(); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: if (_tooltip) DestroyWindow(_tooltip); _win = nullptr; _seg = nullptr; _hint = nullptr; _tooltip = nullptr; _ctl.clear(); _groups.clear(); _titles.clear(); _seps.clear(); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void languageChanged(int lang) {
    if (_seg) { SetWindowLongPtrW(_seg, GWLP_USERDATA, lang ? 0 : 1); InvalidateRect(_seg, nullptr, FALSE); }
    if (_onbWin) InvalidateRect(_onbWin, nullptr, FALSE);
}

void showSettings(HINSTANCE inst, bool firstRun) {
    _inst = inst; initLook(); registerClasses(inst);
    if (firstRun) { showOnboarding(inst); return; }
    if (_win) { bringToFront(_win); return; }
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = mainProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyMain"; wc.hbrBackground = nullptr; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP)); RegisterClassW(&wc); reg = true; }
    _win = CreateWindowExW(0, L"VikeyMain", L"Vikey", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, 0, 0, W, 620, nullptr, nullptr, inst, nullptr);
    applyFluentFrame(_win);
    buildMain();
    centerOnMouseScreen(_win);   // Tony: open in the middle of the screen (Mac 1.0.16/1.0.18)
    bringToFront(_win);
}

// ---- application blacklist ----------------------------------------------------------------
enum { BL_ADD = 800, BL_DONE };
static HWND _blacklistWin, _blacklistList;
static const COLORREF BL_REMOVE_COLOR = RGB(120, 64, 38); // dark brown, intentionally quieter than the primary action

static void refreshBlacklistList() {
    if (!_blacklistList) return;
    ListView_DeleteAllItems(_blacklistList);
    auto apps = core::blacklistedApps();
    for (int i = 0; i < (int)apps.size(); ++i) {
        LVITEMW item = {}; item.mask = LVIF_TEXT; item.iItem = i; item.pszText = (LPWSTR)apps[i].c_str();
        int row = ListView_InsertItem(_blacklistList, &item);
        ListView_SetItemText(_blacklistList, row, 1, (LPWSTR)L"");
    }
}
static LRESULT CALLBACK blacklistProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == BL_ADD) {
            std::wstring current = core::frontAppName();
            if (current.empty() || current == L"UnknownApp") MessageBoxW(h, L"Không xác định được ứng dụng hiện tại.", L"Vikey", MB_OK | MB_ICONWARNING);
            else if (core::isFrontAppBlacklisted()) MessageBoxW(h, L"Ứng dụng này đã có trong danh sách.", L"Vikey", MB_OK | MB_ICONINFORMATION);
            else if (MessageBoxW(h, (L"Thêm " + current + L" vào danh sách không dùng bộ gõ?").c_str(), L"Vikey", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                core::addFrontAppToBlacklist(); refreshBlacklistList();
            }
        } else if (LOWORD(wp) == BL_DONE) DestroyWindow(h);
        return 0;
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lp;
        if (hdr->hwndFrom != _blacklistList) break;
        if (hdr->code == NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
            if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM) && cd->iSubItem == 1)
                return CDRF_NOTIFYPOSTPAINT;
            if (cd->nmcd.dwDrawStage == (CDDS_ITEMPOSTPAINT | CDDS_SUBITEM) && cd->iSubItem == 1) {
                RECT r = cd->nmcd.rc;
                SetBkMode(cd->nmcd.hdc, TRANSPARENT);
                SetTextColor(cd->nmcd.hdc, BL_REMOVE_COLOR);
                HFONT old = (HFONT)SelectObject(cd->nmcd.hdc, fBodyBold);
                DrawTextW(cd->nmcd.hdc, L"×", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(cd->nmcd.hdc, old);
                return CDRF_DODEFAULT;
            }
        } else if (hdr->code == NM_CLICK) {
            NMITEMACTIVATE* hit = (NMITEMACTIVATE*)lp;
            if (hit->iItem >= 0 && hit->iSubItem == 1) {
                wchar_t exe[512] = {};
                ListView_GetItemText(_blacklistList, hit->iItem, 0, exe, 512);
                if (exe[0]) { core::removeAppFromBlacklist(exe); refreshBlacklistList(); }
                return 0;
            }
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: _blacklistWin = nullptr; _blacklistList = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
void showBlacklist(HINSTANCE inst) {
    initLook();
    if (_blacklistWin) { bringToFront(_blacklistWin); refreshBlacklistList(); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = blacklistProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyBlacklist";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
        RegisterClassW(&wc); registered = true;
    }
    RECT wr = {0, 0, 520, 390}; AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    _blacklistWin = CreateWindowExW(0, L"VikeyBlacklist", L"Ứng dụng không dùng bộ gõ", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        0, 0, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
    applyFluentFrame(_blacklistWin);

    _blacklistList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        20, 18, 480, 250, _blacklistWin, nullptr, inst, nullptr);
    ListView_SetExtendedListViewStyle(_blacklistList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(_blacklistList, WM_SETFONT, (WPARAM)fBody, TRUE);
    HWND header = ListView_GetHeader(_blacklistList);
    if (header) SendMessageW(header, WM_SETFONT, (WPARAM)fBodyBold, TRUE);

    LVCOLUMNW nameCol = {}; nameCol.mask = LVCF_TEXT | LVCF_WIDTH; nameCol.pszText = (LPWSTR)L"Tên ứng dụng (EXE)"; nameCol.cx = 432;
    ListView_InsertColumn(_blacklistList, 0, &nameCol);
    LVCOLUMNW removeCol = {}; removeCol.mask = LVCF_TEXT | LVCF_WIDTH; removeCol.pszText = (LPWSTR)L""; removeCol.cx = 44;
    ListView_InsertColumn(_blacklistList, 1, &removeCol);

    HWND note = CreateWindowW(L"STATIC",
        L"Thêm ứng dụng đã mở Vikey trước đó. Đưa ứng dụng khác lên trước rồi mở lại Vikey để thêm ứng dụng đó.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 280, 480, 42, _blacklistWin, nullptr, inst, nullptr);
    SendMessageW(note, WM_SETFONT, (WPARAM)fSmall, TRUE);

    HWND add = CreateWindowW(L"BUTTON", L"Thêm ứng dụng hiện tại", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        20, 338, 190, 32, _blacklistWin, (HMENU)(INT_PTR)BL_ADD, inst, nullptr);
    SendMessageW(add, WM_SETFONT, (WPARAM)fBody, TRUE);
    HWND done = CreateWindowW(L"BUTTON", L"Đóng", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        422, 338, 78, 32, _blacklistWin, (HMENU)(INT_PTR)BL_DONE, inst, nullptr);
    SendMessageW(done, WM_SETFONT, (WPARAM)fBody, TRUE);

    refreshBlacklistList(); centerOnMouseScreen(_blacklistWin); bringToFront(_blacklistWin);
}

// ---- onboarding (OnboardingView.swift: welcome, practice, hotkey; no Accessibility on Windows) --------
enum OnbStep { S_WELCOME, S_PRACTICE, S_HOTKEY, S_COUNT };
static int _step = S_WELCOME;
static const int OW = 540, OH = 470, OP = 28, OCW = OW - 2 * OP;
static const struct { const wchar_t* keys; const wchar_t* result; } PRACTICE[3] = {
    {L"order traf suwax nhes", L"order trà sữa nhé"}, {L"meeting lucs ba giowf nhes", L"meeting lúc ba giờ nhé"}, {L"thanks, camr own nhieeuf", L"thanks, cảm ơn nhiều"}};
static HWND _practiceEdit[3]; static bool _practiceOk[3];
static int _oy[8];   // y positions of the current step's painted parts
static std::wstring normalize(const std::wstring& s) {
    std::wstring t = s; while (!t.empty() && t.back() == L' ') t.pop_back(); size_t a = t.find_first_not_of(L' '); t = a == std::wstring::npos ? L"" : t.substr(a);
    if (t.empty()) return t;
    int n = NormalizeString(NormalizationC, t.c_str(), (int)t.size(), nullptr, 0); std::wstring c(n > 0 ? n : 0, 0);
    if (n > 0) { n = NormalizeString(NormalizationC, t.c_str(), (int)t.size(), &c[0], n); c.resize(n > 0 ? n : 0); } else c = t;
    if (!c.empty()) CharLowerBuffW(&c[0], (DWORD)c.size()); return c;
}
static std::wstring practiceText() {
    return core::language() == 1 ? L"Gõ đúng các phím bên dưới, kể cả tiếng Anh. Vikey bỏ dấu cho tiếng Việt và để nguyên tiếng Anh."
                                 : L"Bạn đang ở tiếng Anh: bấm " + hotkeyDisplay(currentHotkey()) + L" để sang tiếng Việt, rồi gõ các phím bên dưới.";
}
static LRESULT CALLBACK practiceEditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { core::resetTyping(); int i = (int)id; SetFocus(_practiceEdit[std::min(i + 1, 2)]); return 0; }   // onSubmit
    if (m == WM_CHAR && (w == L'\r' || w == L'\n')) return 0;
    return DefSubclassProc(h, m, w, l);
}
static void buildOnboarding();
static void onbGo(int s) { _step = s; buildOnboarding(); }
static HWND onbButton(const wchar_t* text, bool prominent, int x, int w, int id) {
    HWND b = makeChild(_onbWin, L"VikeyButton", text, WS_TABSTOP, x, OH - OP - 32, w, 32, id); SetWindowLongPtrW(b, GWLP_USERDATA, prominent); setBg(b, GROUP); return b;
}
static void buildOnboarding() {
    destroyChildren(_onbWin);
    for (auto& e : _practiceEdit) e = nullptr;
    int y = 98;   // under the header (icon 48 + 22)
    switch (_step) {
    case S_WELCOME: {
        _oy[0] = y; y += 28 + 14;
        _oy[1] = y; y += textHeight(fBody, L"Vikey chỉ hỗ trợ Telex, kiểu gõ phổ biến nhất. Bạn gõ tiếng Anh bình thường, Vikey tự nhận ra và không bỏ dấu nhầm.", OCW) + 14;
        _oy[2] = y;   // the Telex table
        break; }
    case S_PRACTICE: {
        _oy[0] = y; y += 28 + 12;
        _oy[1] = y; y += 2 * 20 + 12;   // both texts take two lines: switching VI/EN must not move the fields (Tony)
        for (int i = 0; i < 3; i++) {
            _oy[2 + i] = y;
            HWND e = makeChild(_onbWin, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, OP, y + 22, OCW - 34, 28, O_EDIT + i, fBody);
            SetWindowSubclass(e, practiceEditProc, i, 0);
            SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
            _practiceEdit[i] = e; _practiceOk[i] = false;
            y += 22 + 28 + 12;
        }
        SetFocus(_practiceEdit[0]);
        break; }
    case S_HOTKEY: {
        _oy[0] = y; y += 28 + 14;
        std::wstring t = L"Vikey tự chuyển khi bạn gõ lẫn tiếng Anh. Khi muốn tự chuyển, bấm phím tắt này. Bấm vào ô để đổi phím.";
        _oy[1] = y; y += textHeight(fBody, t, OCW) + 14;
        makeRecorder(_onbWin, L"SwitchKeyStatus", currentHotkey(), OP, y, 150, 34, O_REC, GROUP);
        _oy[2] = y; y += 34 + 14;
        _oy[3] = y;
        HDC dc = GetDC(_onbWin); int lw = textWidth(dc, fBody, L"Âm báo khi chuyển"); ReleaseDC(_onbWin, dc);
        HWND tg = makeChild(_onbWin, L"VikeyToggle", L"", WS_TABSTOP, OP + lw + 12, y, 40, 23, O_BEEP); setBg(tg, GROUP); setToggle(tg, HAS_BEEP(currentHotkey()) != 0);
        break; }
    }
    // footer
    if (_step != S_WELCOME) onbButton(L"Quay lại", false, OP, 100, O_BACK);
    const wchar_t* next = _step == S_WELCOME ? L"Bắt đầu" : _step == S_HOTKEY ? L"Xong" : L"Tiếp tục";
    HWND nb = onbButton(next, true, OW - OP - 110, 110, O_NEXT);
    InvalidateRect(_onbWin, nullptr, TRUE);
}
static void drawCheck(Graphics& g, float x, float y, bool ok) {
    if (ok) { SolidBrush b(gp(GREEN)); g.FillEllipse(&b, x, y, 22.0f, 22.0f); Pen pen(Color(255, 255, 255, 255), 2.4f); g.DrawLine(&pen, x + 6, y + 11.5f, x + 9.5f, y + 15); g.DrawLine(&pen, x + 9.5f, y + 15, x + 16, y + 7.5f); }
    else { Pen p(gp(RGB(200, 200, 204)), 1.6f); g.DrawEllipse(&p, x + 1, y + 1, 20.0f, 20.0f); }
}
static void paintOnboarding(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(GROUP); FillRect(mem, &rc, bg); DeleteObject(bg);
    Graphics g(mem); g.SetSmoothingMode(SmoothingModeAntiAlias);
    SetBkMode(mem, TRANSPARENT);
    // header: icon, wordmark, tagline, step dots
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    if (icon) { DrawIconEx(mem, OP, OP, icon, 48, 48, 0, nullptr, DI_NORMAL); DestroyIcon(icon); }
    drawWordmark(mem, fWordmark, OP + 62, OP - 3);
    SelectObject(mem, fBody); SetTextColor(mem, SECONDARY); RECT tr = {OP + 62, OP + 27, OW - OP - 60, OP + 49}; DrawTextW(mem, L"Gõ tiếng Việt, tự chuyển Anh/Việt cho bạn.", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    for (int i = 0; i < S_COUNT; i++) { SolidBrush b(i == _step ? gp(BRAND) : gp(RGB(205, 205, 210))); g.FillEllipse(&b, (float)(OW - OP - (S_COUNT - i) * 13 + 6), (float)(OP + 20), 7.0f, 7.0f); }
    auto title = [&](const wchar_t* t, int y) { SelectObject(mem, fTitle2); SetTextColor(mem, TEXT); RECT r = {OP, y, OW - OP, y + 28}; DrawTextW(mem, t, -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX); return textWidth(mem, fTitle2, t); };
    auto para = [&](const std::wstring& t, int y, int hgt) { SelectObject(mem, fBody); SetTextColor(mem, SECONDARY); RECT r = {OP, y, OW - OP, y + hgt}; DrawTextW(mem, t.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX); };
    auto label = [&](const wchar_t* t, int x, int y, int hgt, COLORREF c = TEXT, HFONT f = nullptr) { SelectObject(mem, f ? f : fBody); SetTextColor(mem, c); RECT r = {x, y, OW - OP, y + hgt}; DrawTextW(mem, t, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); };
    switch (_step) {
    case S_WELCOME: {
        title(L"Vikey dùng kiểu gõ Telex", _oy[0]);
        para(L"Vikey chỉ hỗ trợ Telex, kiểu gõ phổ biến nhất. Bạn gõ tiếng Anh bình thường, Vikey tự nhận ra và không bỏ dấu nhầm.", _oy[1], 60);
        static const wchar_t* T[4][4][2] = {{{L"aa", L"â"}, {L"aw", L"ă"}, {L"ee", L"ê"}, {L"oo", L"ô"}}, {{L"ow", L"ơ"}, {L"uw", L"ư"}, {L"dd", L"đ"}, {L"w", L"ư"}},
                                            {{L"s", L"sắc"}, {L"f", L"huyền"}, {L"r", L"hỏi"}, {L"x", L"ngã"}}, {{L"j", L"nặng"}, {L"z", L"xoá dấu"}, {nullptr, nullptr}, {nullptr, nullptr}}};
        int top = _oy[2], rowH = 32, boxH = 14 * 2 + 4 * rowH - 8;
        roundRect(g, RectF((float)OP, (float)top, (float)OCW, (float)boxH), 10, gp(RGB(242, 242, 244)), Color(0, 0, 0, 0));
        int colW = (OCW - 28) / 4;
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
            if (!T[r][c][0]) continue;
            int x = OP + 14 + c * colW, y = top + 14 + r * rowH;
            int kw = textWidth(mem, fMonoBold, T[r][c][0]) + 12;
            roundRect(g, RectF((float)x, (float)y, (float)kw, 24.0f), 5, gp(GROUP), Color(0, 0, 0, 0));
            SelectObject(mem, fMonoBold); SetTextColor(mem, TEXT); RECT kr = {x, y, x + kw, y + 24}; DrawTextW(mem, T[r][c][0], -1, &kr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            label(L"→", x + kw + 6, y, 24, TERTIARY, fSmall);
            label(T[r][c][1], x + kw + 22, y, 24);
        }
        break; }
    case S_PRACTICE: {
        title(L"Gõ thử", _oy[0]);
        para(practiceText(), _oy[1], 40);
        for (int i = 0; i < 3; i++) {
            int y = _oy[2 + i], x = OP;
            label(L"Gõ", x, y, 20, SECONDARY, fCallout); x += textWidth(mem, fCallout, L"Gõ") + 6;
            label(PRACTICE[i].keys, x, y, 20, TEXT, fMonoBold); x += textWidth(mem, fMonoBold, PRACTICE[i].keys) + 6;
            label(L"→", x, y, 20, TERTIARY, fSmall); x += textWidth(mem, fSmall, L"→") + 6;
            label(PRACTICE[i].result, x, y, 20, TEXT, fCallout);
            drawCheck(g, (float)(OW - OP - 22), (float)(y + 22 + 3), _practiceOk[i]);
        }
        break; }
    case S_HOTKEY: {
        title(L"Phím chuyển Anh/Việt", _oy[0]);
        para(L"Vikey tự chuyển khi bạn gõ lẫn tiếng Anh. Khi muốn tự chuyển, bấm phím tắt này. Bấm vào ô để đổi phím.", _oy[1], 60);
        label(core::language() == 1 ? L"Đang gõ: Tiếng Việt" : L"Đang gõ: English", OP + 150 + 12, _oy[2], 34, SECONDARY);
        label(L"Âm báo khi chuyển", OP, _oy[3], 23);
        break; }
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(h, &ps);
}
static LRESULT CALLBACK onbProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: paintOnboarding(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND: {
        int id = LOWORD(wp); HWND c = (HWND)lp;
        if (id >= O_EDIT && id < O_EDIT + 3 && HIWORD(wp) == EN_CHANGE) {
            int i = id - O_EDIT; wchar_t buf[256]; GetWindowTextW(c, buf, 256);
            bool ok = normalize(buf) == normalize(PRACTICE[i].result);
            if (ok != _practiceOk[i]) { _practiceOk[i] = ok; InvalidateRect(h, nullptr, FALSE);
                if (ok && i + 1 < 3) { core::resetTyping(); SetFocus(_practiceEdit[i + 1]); } }   // the next field starts a new word, as after a click
            return 0;
        }
        switch (id) {
        case O_BACK: if (_step > 0) onbGo(_step - 1); break;
        case O_NEXT: if (_step == S_HOTKEY) { prefs::setInt(L"OnboardingDone", 1); DestroyWindow(h); } else onbGo(_step + 1); break;
        case O_REC: InvalidateRect(h, nullptr, FALSE); if (_win) PostMessageW(_win, WM_REBUILD, 0, 0); break;
        case O_BEEP: { int hk = (currentHotkey() & ~0x8000) | (toggleOn(c) ? 0x8000 : 0); prefs::setInt(L"SwitchKeyStatus", hk); core::reloadSettings(); if (_win) PostMessageW(_win, WM_REBUILD, 0, 0); break; }
        }
        return 0; }
    case WM_REBUILD: buildOnboarding(); return 0;
    case WM_CTLCOLOREDIT: return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: _onbWin = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
void showOnboarding(HINSTANCE inst) {
    _inst = inst; initLook(); registerClasses(inst);
    if (_onbWin) { bringToFront(_onbWin); return; }   // Mac: opened again later starts from the first step; an open one just comes forward
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = onbProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyOnboarding"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP)); RegisterClassW(&wc); reg = true; }
    RECT wr = {0, 0, OW, OH}; AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    _onbWin = CreateWindowExW(0, L"VikeyOnboarding", L"Vikey", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, 0, 0, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
    applyFluentFrame(_onbWin);
    int s = prefs::getInt(L"OnboardingStep", -1); _step = s >= 0 && s < S_COUNT ? s : S_WELCOME;   // screenshots
    buildOnboarding();
    centerOnMouseScreen(_onbWin);
    bringToFront(_onbWin);
    prefs::setInt(L"FirstRunDone", 1);
}
/// A hotkey changed in one window: the other one shows it too.
static void afterHotkeyChange() {
    if (_win) { setHint(); PostMessageW(_win, WM_REBUILD, 0, 0); }
    if (_onbWin && _step != S_PRACTICE) PostMessageW(_onbWin, WM_REBUILD, 0, 0); else if (_onbWin) InvalidateRect(_onbWin, nullptr, FALSE);
}

// ---- acknowledgements (AcknowledgementsView.swift; App/Model/Acknowledgements.swift minus Sparkle) ----------------
struct Credit { const wchar_t* name; const wchar_t* by; const wchar_t* license; const wchar_t* url; };
static const Credit ACK_CODE[] = {
    {L"OpenKey", L"Tuyen Mai", L"GPL-3.0", L"https://github.com/tuyenvm/OpenKey"},
    {L"TweetNaCl", L"Daniel J. Bernstein et al.", L"public domain", L"https://tweetnacl.cr.yp.to"},
    {L"Inno Setup", L"Jordan Russell, Martijn Laan", L"Inno Setup License", L"https://jrsoftware.org/isinfo.php"},
};
static const Credit ACK_WORDS[] = {
    {L"FrequencyWords (OpenSubtitles 2018)", L"Hermit Dave", L"CC BY-SA 4.0", L"https://github.com/hermitdave/FrequencyWords"},
    {L"FineWeb-2 and FineWeb", L"Hugging Face", L"ODC-By 1.0", L"https://huggingface.co/datasets/HuggingFaceFW/fineweb-2"},
    {L"HPLT v2", L"High Performance Language Technologies", L"CC0", L"https://hplt-project.org"},
    {L"MADLAD-400", L"Google", L"ODC-By 1.0", L"https://huggingface.co/datasets/allenai/MADLAD-400"},
    {L"Reddit comments", L"tensorshield/reddit_dataset_157", L"MIT", L"https://huggingface.co/datasets/tensorshield/reddit_dataset_157"},
    {L"Vietnamese forums and reader comments", L"VnExpress, Tuổi Trẻ, Tinhte, Otofun, Dạy Nhau Học", L"word statistics", L"https://vikey.app/acknowledgements"},
};
static const Credit ACK_TEST[] = {
    {L"ViLexNorm", L"Nguyen et al., EACL 2024", L"CC BY-NC-SA 4.0", L"https://github.com/ngxtnhi/ViLexNorm"},
    {L"UIT-ViCTSD", L"UIT NLP Group", L"research use", L"https://huggingface.co/datasets/tarudesu/ViCTSD"},
    {L"VietMix", L"EACL 2026", L"research use", L"https://huggingface.co/datasets/razent/vietmix"},
    {L"Tap typing with touch sensing images", L"Google Research", L"CC BY 4.0", L"https://github.com/google-research-datasets/tap-typing-with-touch-sensing-images"},
    {L"How We Type: Mobile", L"Aalto University", L"research use", L"https://userinterfaces.aalto.fi/how-we-type-mobile"},
    {L"GitHub Typo Corpus", L"Hagiwara & Mita, LREC 2020", L"research use", L"https://github.com/mhagiwara/github-typo-corpus"},
    {L"Birkbeck spelling error corpus", L"Roger Mitton", L"free to use", L"https://www.dcs.bbk.ac.uk/~roger/corpora.html"},
    {L"Lists of common misspellings", L"Wikipedia", L"CC BY-SA 4.0", L"https://en.wikipedia.org/wiki/Wikipedia:Lists_of_common_misspellings"},
};
static const int AW = 520, AH = 600, AP = 24;
static int _ackScroll = 0, _ackContent = 0; static std::vector<Link> _ackLinks;
static int paintAck(HDC dc, int top, bool draw) {   // returns the content height
    int y = top + AP; _ackLinks.clear();
    struct Sec { const wchar_t* title; const wchar_t* note; const Credit* items; int n; } secs[] = {
        {L"Mã nguồn", L"Vikey dùng mã nguồn của:", ACK_CODE, (int)(sizeof(ACK_CODE) / sizeof(*ACK_CODE))},
        {L"Dữ liệu từ ngữ", L"Vikey tự chuyển Anh/Việt nhờ thống kê từ ngữ đếm từ các nguồn này. Trong ứng dụng chỉ có số liệu thống kê, không có văn bản nào.", ACK_WORDS, (int)(sizeof(ACK_WORDS) / sizeof(*ACK_WORDS))},
        {L"Dữ liệu kiểm thử", L"Chỉ dùng để đo và cải thiện độ chính xác, không có trong ứng dụng.", ACK_TEST, (int)(sizeof(ACK_TEST) / sizeof(*ACK_TEST))}};
    SetBkMode(dc, TRANSPARENT);
    for (int si = 0; si < 3; si++) {
        auto& s = secs[si];
        if (draw) { SelectObject(dc, fTitle); SetTextColor(dc, TEXT); RECT r = {AP, y, AW - AP, y + 22}; DrawTextW(dc, s.title, -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX); }
        y += 22 + 6;
        int nh = textHeight(fCallout, s.note, AW - 2 * AP);
        if (draw) { SelectObject(dc, fCallout); SetTextColor(dc, SECONDARY); RECT r = {AP, y, AW - AP, y + nh}; DrawTextW(dc, s.note, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX); }
        y += nh + 8;
        for (int i = 0; i < s.n; i++) {
            auto& c = s.items[i];
            int lw = textWidth(dc, fSmall, c.license);
            if (draw) {
                std::vector<Seg> name = {{c.name, c.url, fBody, ACCENT}}; drawSegs(dc, name, AP, y, AW - 2 * AP - lw - 12, false, _ackLinks);
                SelectObject(dc, fSmall); SetTextColor(dc, SECONDARY);
                RECT lr = {AW - AP - lw, y + 2, AW - AP, y + 20}; DrawTextW(dc, c.license, -1, &lr, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                RECT br = {AP, y + 21, AW - AP - lw - 12, y + 38}; DrawTextW(dc, c.by, -1, &br, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            y += 40;
        }
        y += 14;
    }
    return y - top + AP - 10;
}
static LRESULT CALLBACK ackProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(GROUP); FillRect(mem, &rc, bg); DeleteObject(bg);
        paintAck(mem, -_ackScroll, true);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEWHEEL: case WM_VSCROLL: {
        RECT rc; GetClientRect(h, &rc); int maxS = std::max(0, _ackContent - (int)rc.bottom), s = _ackScroll;
        if (msg == WM_MOUSEWHEEL) s -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 60;
        else switch (LOWORD(wp)) { case SB_LINEUP: s -= 30; break; case SB_LINEDOWN: s += 30; break; case SB_PAGEUP: s -= rc.bottom; break; case SB_PAGEDOWN: s += rc.bottom; break; case SB_THUMBTRACK: case SB_THUMBPOSITION: s = HIWORD(wp); break; case SB_TOP: s = 0; break; case SB_BOTTOM: s = maxS; break; }
        _ackScroll = std::max(0, std::min(s, maxS)); SetScrollPos(h, SB_VERT, _ackScroll, TRUE); InvalidateRect(h, nullptr, FALSE); return 0; }
    case WM_SETCURSOR: { POINT p; GetCursorPos(&p); ScreenToClient(h, &p); if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, linkAt(_ackLinks, MAKELPARAM(p.x, p.y)) ? IDC_HAND : IDC_ARROW)); return TRUE; } break; }
    case WM_LBUTTONUP: if (const Link* l = linkAt(_ackLinks, lp)) ShellExecuteW(nullptr, L"open", l->url.c_str(), nullptr, nullptr, SW_SHOW); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: _ackWin = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
void showAcknowledgements(HINSTANCE inst) {
    initLook();
    if (_ackWin) { bringToFront(_ackWin); return; }
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = ackProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyAck"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP)); RegisterClassW(&wc); reg = true; }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    RECT wr = {0, 0, AW, AH}; AdjustWindowRectEx(&wr, style, FALSE, 0); wr.right += GetSystemMetrics(SM_CXVSCROLL);
    _ackWin = CreateWindowExW(0, L"VikeyAck", L"Ghi nhận", style, 0, 0, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
    applyFluentFrame(_ackWin);
    HDC dc = GetDC(_ackWin); _ackContent = paintAck(dc, 0, false); ReleaseDC(_ackWin, dc); _ackScroll = 0;
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, _ackContent - 1, (UINT)AH, 0}; SetScrollInfo(_ackWin, SB_VERT, &si, TRUE);
    centerOnMouseScreen(_ackWin);
    bringToFront(_ackWin);
}

// ---- about (AboutPanel: the standard About window with the author, the OpenKey credit and links) --------------
static std::vector<Link> _aboutLinks;
static LRESULT CALLBACK aboutProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(GROUP); FillRect(mem, &rc, bg); DeleteObject(bg);
        HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 96, 96, LR_DEFAULTCOLOR);
        if (icon) { DrawIconEx(mem, (rc.right - 96) / 2, 24, icon, 96, 96, 0, nullptr, DI_NORMAL); DestroyIcon(icon); }
        SetBkMode(mem, TRANSPARENT); SelectObject(mem, fWordmark); SetTextColor(mem, TEXT); RECT r = {0, 130, rc.right, 160}; DrawTextW(mem, L"Vikey", -1, &r, DT_CENTER | DT_SINGLELINE);
        SelectObject(mem, fSmall); SetTextColor(mem, SECONDARY); RECT r2 = {0, 162, rc.right, 182}; DrawTextW(mem, L"Phiên bản " VIKEY_VERSION_W, -1, &r2, DT_CENTER | DT_SINGLELINE);
        _aboutLinks.clear(); int y = 196;
        std::vector<std::vector<Seg>> lines = {
            {{L"Tony Dinh", L"", fSmall, SECONDARY}},
            {{L"Dựa trên ", L"", fSmall, SECONDARY}, {L"OpenKey", L"https://github.com/tuyenvm/OpenKey", fSmall, ACCENT}, {L" của Tuyen Mai", L"", fSmall, SECONDARY}},
            {{L"Mã nguồn mở theo giấy phép GPL-3.0", L"", fSmall, SECONDARY}},
            {},
            {{L"vikey.app", L"https://vikey.app", fSmall, ACCENT}, {L"  ·  ", L"", fSmall, SECONDARY}, {L"GitHub", L"https://github.com/tonyminibot/Vikey", fSmall, ACCENT}, {L"  ·  ", L"", fSmall, SECONDARY}, {L"Ghi nhận", L"ack:", fSmall, ACCENT}}};
        for (auto& l : lines) { if (l.empty()) { y += 8; continue; } y += drawSegs(mem, l, 0, y, rc.right, true, _aboutLinks) + 1; }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: { POINT p; GetCursorPos(&p); ScreenToClient(h, &p); if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, linkAt(_aboutLinks, MAKELPARAM(p.x, p.y)) ? IDC_HAND : IDC_ARROW)); return TRUE; } break; }
    case WM_LBUTTONUP: if (const Link* l = linkAt(_aboutLinks, lp)) { if (l->url == L"ack:") showAcknowledgements(_inst); else ShellExecuteW(nullptr, L"open", l->url.c_str(), nullptr, nullptr, SW_SHOW); } return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: _aboutWin = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
void showAbout(HINSTANCE inst) {
    _inst = inst; initLook();
    if (_aboutWin) { bringToFront(_aboutWin); return; }
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = aboutProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyAbout"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP)); RegisterClassW(&wc); reg = true; }
    RECT wr = {0, 0, 300, 304}; AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    _aboutWin = CreateWindowExW(0, L"VikeyAbout", L"Giới thiệu Vikey", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 0, 0, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
    applyFluentFrame(_aboutWin);
    centerOnMouseScreen(_aboutWin);
    bringToFront(_aboutWin);
}

// ---- typing log window ------------------------------------------------------------------------
static std::wstring fromUtf8(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w; }
static std::wstring jget(const std::wstring& j, const wchar_t* key) {   // flat JSON string field
    std::wstring k = L"\"" + std::wstring(key) + L"\":"; size_t p = j.find(k); if (p == std::wstring::npos) return L"";
    p += k.size(); if (p < j.size() && j[p] == L'"') { size_t e = p + 1; std::wstring out; while (e < j.size() && j[e] != L'"') { if (j[e] == L'\\' && e + 1 < j.size()) { e++; if (j[e] == L'u' && e + 4 < j.size()) { out += (wchar_t)wcstol(j.substr(e + 1, 4).c_str(), nullptr, 16); e += 4; } else out += j[e]; } else out += j[e]; e++; } return out; }
    size_t e = j.find_first_of(L",}", p); return j.substr(p, e - p);
}
struct Period { std::map<std::wstring, int> c; int get(const wchar_t* k) const { auto i = c.find(k); return i == c.end() ? 0 : i->second; } };
static Period sumStats(int days) {   // TLStat_<day>_<name> registry values
    Period p; HKEY h; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vikey", 0, KEY_READ, &h) != ERROR_SUCCESS) return p;
    SYSTEMTIME st; GetLocalTime(&st); FILETIME ft; SystemTimeToFileTime(&st, &ft); ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    std::wstring from = L"0000";
    if (days > 0) { u.QuadPart -= (ULONGLONG)(days - 1) * 864000000000ULL; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart; SYSTEMTIME f; FileTimeToSystemTime(&ft, &f); wchar_t b[16]; swprintf_s(b, L"%04d-%02d-%02d", f.wYear, f.wMonth, f.wDay); from = b; }
    for (DWORD i = 0;; i++) { wchar_t name[256]; DWORD nl = 256, type, v, vs = sizeof(v); LONG rc = RegEnumValueW(h, i, name, &nl, nullptr, &type, (LPBYTE)&v, &vs);
        if (rc == ERROR_MORE_DATA) continue;   // a longer value (the smart-switch table): not a counter
        if (rc != ERROR_SUCCESS) break;
        std::wstring n = name; if (n.rfind(L"TLStat_", 0) != 0 || type != REG_DWORD) continue; std::wstring day = n.substr(7, 10); if (day < from) continue; p.c[n.substr(18)] += (int)v; }
    RegCloseKey(h); return p;
}
static void refreshLog() {
    core::flushTypingLog();
    Period p1 = sumStats(1), p7 = sumStats(7), pa = sumStats(0);
    auto autoN = [](const Period& p) { return p.get(L"auto_vi_word_to_en") + p.get(L"auto_en_word_to_vi") + p.get(L"auto_double_key"); };
    wchar_t s[200]; swprintf_s(s, L"Hôm nay: %d lần chuyển tay, %d lần tự chuyển", p1.get(L"manual"), autoN(p1)); SetWindowTextW(_logSummary, s);
    const wchar_t* rows[][2] = {{L"Từ đã gõ", L"words"}, {L"Chuyển tay", L"manual"}, {L"Tự chuyển", L"auto"}, {L"    Tiếng Việt → Anh (cả từ)", L"auto_vi_word_to_en"}, {L"    Anh → Tiếng Việt (cả từ)", L"auto_en_word_to_vi"},
                               {L"    Gõ dấu hai lần → Anh", L"auto_double_key"}, {L"Hoàn tác bằng Delete", L"undo"}, {L"Đổi theo ứng dụng / trang web", L"context"}, {L"Chuyển tay mỗi 100 từ", L"rate"}};
    ListView_DeleteAllItems(_logGrid);
    const Period* ps[3] = {&p1, &p7, &pa};
    for (int r = 0; r < 9; r++) {
        LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = r; it.pszText = (LPWSTR)rows[r][0]; ListView_InsertItem(_logGrid, &it);
        for (int c = 0; c < 3; c++) {
            const Period& p = *ps[c]; int words = p.get(L"words_vi") + p.get(L"words_en"); std::wstring v; wchar_t b[32];
            std::wstring key = rows[r][1];
            if (key == L"words") v = std::to_wstring(words); else if (key == L"auto") v = std::to_wstring(autoN(p));
            else if (key == L"rate") { if (words) { swprintf_s(b, L"%.1f", 100.0 * p.get(L"manual") / words); v = b; } else v = L"–"; }
            else v = std::to_wstring(p.get(key.c_str()));
            ListView_SetItemText(_logGrid, r, c + 1, (LPWSTR)v.c_str());
        }
    }
    // events, newest first
    std::ifstream f(core::typingLogPath(), std::ios::binary); std::vector<std::string> lines; std::string l;
    while (std::getline(f, l)) { if (!l.empty()) lines.push_back(l); if (lines.size() > 2000) lines.erase(lines.begin(), lines.begin() + 1000); }
    ListView_DeleteAllItems(_logList);
    int n = 0;
    for (auto li = lines.rbegin(); li != lines.rend() && n < 300; ++li, ++n) {
        std::wstring j = fromUtf8(*li); std::wstring e = jget(j, L"e"), to = jget(j, L"to") == L"vi" ? L"Tiếng Việt" : L"Anh", t = jget(j, L"t");
        std::wstring time = t.size() >= 19 ? t.substr(5, 14) : t; for (auto& ch : time) if (ch == L'T') ch = L' ';
        std::wstring app = jget(j, L"app"), site = jget(j, L"site"); std::wstring place = site.empty() ? app : app + L" · " + site;
        std::wstring title, detail;
        if (e == L"word") { title = jget(j, L"lang") == L"vi" ? L"Từ (Việt)" : L"Từ (Anh)"; detail = jget(j, L"keys") + L"   [" + jget(j, L"end") + L"]"; }
        else if (e == L"auto") { title = L"Tự chuyển → " + to; std::wstring r = jget(j, L"rule"); detail = r == L"vi_word_to_en" ? L"từ tiếng Anh gõ khi đang ở Tiếng Việt" : r == L"en_word_to_vi" ? L"từ tiếng Việt gõ khi đang ở Anh" : L"gõ dấu hai lần"; }
        else if (e == L"manual") { title = L"Chuyển tay → " + to; detail = (jget(j, L"via") == L"menu" ? L"bằng menu" : L"bằng phím tắt") + std::wstring(jget(j, L"after_auto") == L"true" ? L", ngay sau tự chuyển" : L""); }
        else if (e == L"undo") { title = L"Hoàn tác → " + to; detail = L"bằng Delete"; }
        else if (e == L"context") { title = L"Theo app/web → " + to; }
        else { title = e; }
        LVITEMW it = {}; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = n; it.pszText = (LPWSTR)time.c_str(); it.lParam = e == L"manual" || e == L"undo" ? 1 : (e == L"word" ? 0 : 2); ListView_InsertItem(_logList, &it);
        ListView_SetItemText(_logList, n, 1, (LPWSTR)place.c_str()); ListView_SetItemText(_logList, n, 2, (LPWSTR)title.c_str()); ListView_SetItemText(_logList, n, 3, (LPWSTR)detail.c_str());
    }
}
static LRESULT CALLBACK logProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == L_REFRESH) refreshLog();
        else if (LOWORD(wp) == L_OPEN) { core::flushTypingLog(); std::wstring p = core::typingLogPath(); if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + p + L"\"").c_str(), nullptr, SW_SHOW); else ShellExecuteW(nullptr, L"open", prefs::appDataDir().c_str(), nullptr, nullptr, SW_SHOW); }
        else if (LOWORD(wp) == L_CLEAR) { if (MessageBoxW(h, L"Số liệu thống kê và file log sẽ bị xoá.", L"Xoá toàn bộ nhật ký gõ?", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK) { core::clearTypingLog(); HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vikey", 0, KEY_READ | KEY_WRITE, &k) == ERROR_SUCCESS) { std::vector<std::wstring> del; for (DWORD i = 0;; i++) { wchar_t name[256]; DWORD nl = 256; if (RegEnumValueW(k, i, name, &nl, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break; if (wcsncmp(name, L"TLStat_", 7) == 0) del.push_back(name); } for (auto& d : del) RegDeleteValueW(k, d.c_str()); RegCloseKey(k); } refreshLog(); } }
        else if (LOWORD(wp) == C_LOGKEYS) { prefs::setInt(L"TypingLogKeys", toggleOn((HWND)lp)); core::reloadSettings(); }
        return 0;
    case WM_NOTIFY: { NMHDR* n = (NMHDR*)lp;
        if (n->hwndFrom == _logList && n->code == NM_CUSTOMDRAW) { NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) { cd->clrText = cd->nmcd.lItemlParam == 1 ? RGB(255, 149, 0) : (cd->nmcd.lItemlParam == 2 ? ACCENT : TEXT); return CDRF_NEWFONT; } }
        return 0; }
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); SetTextColor((HDC)wp, (HWND)lp == _logSummary ? TEXT : SECONDARY); return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_SIZE: { RECT r; GetClientRect(h, &r); MoveWindow(_logList, 20, 390, r.right - 40, r.bottom - 390 - 56, TRUE); for (int id : {L_REFRESH, L_OPEN, L_CLEAR}) { HWND b = GetDlgItem(h, id); RECT br; GetWindowRect(b, &br); MapWindowPoints(nullptr, h, (POINT*)&br, 2); MoveWindow(b, id == L_CLEAR ? r.right - 20 - 100 : br.left, r.bottom - 44, id == L_CLEAR ? 100 : br.right - br.left, 28, TRUE); } return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: _logWin = nullptr; return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
void showTypingLog(HINSTANCE inst) {
    initLook(); registerClasses(inst);
    if (_logWin) { bringToFront(_logWin); refreshLog(); return; }
    static bool reg = false;
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = logProc; wc.hInstance = inst; wc.lpszClassName = L"VikeyLog"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP)); RegisterClassW(&wc); reg = true; }
    RECT wr = {0, 0, 760, 600}; AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
    _logWin = CreateWindowExW(0, L"VikeyLog", L"Nhật ký gõ", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
    HWND old = _win; _win = _logWin;
    _logSummary = child(L"STATIC", L"", SS_LEFT, 20, 20, 700, 24, 0, fTitle);
    _logGrid = child(WC_LISTVIEWW, L"", LVS_REPORT | LVS_NOSORTHEADER | LVS_SINGLESEL | LVS_NOCOLUMNHEADER * 0, 20, 52, 720, 252, 0, fBody);
    ListView_SetExtendedListViewStyle(_logGrid, LVS_EX_FULLROWSELECT);
    const wchar_t* heads[] = {L"", L"Hôm nay", L"7 ngày", L"Tất cả"}; int widths[] = {330, 120, 120, 120};
    for (int i = 0; i < 4; i++) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; c.pszText = (LPWSTR)heads[i]; c.cx = widths[i]; c.fmt = i ? LVCFMT_RIGHT : LVCFMT_LEFT; ListView_InsertColumn(_logGrid, i, &c); }
    HWND tg = child(L"VikeyToggle", L"", 0, 20, 314, 40, 23, C_LOGKEYS); setToggle(tg, prefs::getInt(L"TypingLogKeys", 1) != 0);
    child(L"STATIC", L"Ghi lại phím gõ của từng từ", SS_LEFT, 68, 316, 500, 20, 0, fBody);
    child(L"STATIC", L"Không bao giờ ghi trong ô mật khẩu, trình quản lý mật khẩu và Terminal.", SS_LEFT, 68, 338, 600, 18, 1, fSmall);
    child(L"STATIC", L"Sự kiện gần đây", SS_LEFT, 20, 366, 300, 20, 0, fBodyBold);
    _logList = child(WC_LISTVIEWW, L"", LVS_REPORT | LVS_NOSORTHEADER | LVS_SINGLESEL, 20, 390, 720, 150, 0, fBody);
    ListView_SetExtendedListViewStyle(_logList, LVS_EX_FULLROWSELECT);
    const wchar_t* eh[] = {L"Thời gian", L"Ứng dụng / trang", L"Sự kiện", L"Chi tiết"}; int ew[] = {110, 160, 140, 290};
    for (int i = 0; i < 4; i++) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)eh[i]; c.cx = ew[i]; ListView_InsertColumn(_logList, i, &c); }
    child(L"BUTTON", L"Làm mới", 0, 20, 556, 90, 28, L_REFRESH, fBody); child(L"BUTTON", L"Mở file log", 0, 118, 556, 100, 28, L_OPEN, fBody); child(L"BUTTON", L"Xoá log…", 0, 640, 556, 100, 28, L_CLEAR, fBody);
    _win = old;
    refreshLog(); centerOnMouseScreen(_logWin); bringToFront(_logWin);
}

}} // namespace
