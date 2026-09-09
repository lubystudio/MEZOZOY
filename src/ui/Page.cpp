#include "Page.h"

#include "Win32Util.h"
#include "UiStyle.h"

#include <richedit.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>

namespace mezozoy::ui {

NativePage::NativePage(HINSTANCE instance, HWND parent, const Theme& theme) : instance_(instance), parent_(parent), theme_(theme) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc; wc.hInstance = instance_; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; wc.lpszClassName = L"Mezozoy.NativePage"; wc.style = 0;
        RegisterClassExW(&wc); registered = true;
    }
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    panelBrush_ = CreateSolidBrush(theme_.panel);
    editBrush_ = CreateSolidBrush(theme_.page);
    uiFont_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    uiFontBold_ = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    hwnd_ = CreateWindowExW(0, L"Mezozoy.NativePage", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, 0, 0, parent_, nullptr, instance_, this);
    style::Attach(hwnd_, &theme_);
}

NativePage::~NativePage() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    if (uiFont_) DeleteObject(uiFont_); if (uiFontBold_) DeleteObject(uiFontBold_);
    if (backgroundBrush_) DeleteObject(backgroundBrush_); if (panelBrush_) DeleteObject(panelBrush_); if (editBrush_) DeleteObject(editBrush_);
}

void NativePage::remember(HWND control, bool bold) {
    if (!control) return;
    controls_.push_back(control);
    if (bold) boldControls_.push_back(control);
    SetFont(control, bold ? uiFontBold_ : uiFont_);
    wchar_t className[32]{};
    GetClassNameW(control, className, static_cast<int>(std::size(className)));
    style::Attach(control, &theme_);
    if (_wcsicmp(className, L"Edit") == 0 || _wcsicmp(className, L"ListBox") == 0) {
        SetWindowLongPtrW(control, GWL_EXSTYLE, GetWindowLongPtrW(control, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
        LONG_PTR bits = GetWindowLongPtrW(control, GWL_STYLE);
        if (_wcsicmp(className, L"Edit") == 0) {
            bits |= WS_BORDER;
            SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8,8));
        } else if (!(bits & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE))) {
            bits = (bits & ~WS_BORDER) | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
            SetPropW(control, L"Mezozoy.Ui.List", reinterpret_cast<HANDLE>(1));
        }
        SetWindowLongPtrW(control, GWL_STYLE, bits);
        SetWindowPos(control, nullptr, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (GetPropW(control,L"Mezozoy.Ui.List")) SendMessageW(control, LB_SETITEMHEIGHT, 0, style::RowHeight);
    }
    if (_wcsicmp(className, L"Button") == 0) {
        const LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
        const LONG_PTR type = style & BS_TYPEMASK;
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON)
            SetWindowLongPtrW(control, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
        else if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX || type == BS_AUTO3STATE || type == BS_3STATE)
            SetWindowTheme(control, L"", L"");
    }
    SendMessageW(control, WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
}

void NativePage::layout(int width, int height) {
    layoutAt(0, 0, width, height);
}

void NativePage::layoutAt(int x, int y, int width, int height) {
    width = std::max(0, width);
    height = std::max(0, height);
    const bool positionChanged = x != layoutX_ || y != layoutY_;
    const bool sizeChanged = width != layoutWidth_ || height != layoutHeight_;
    if (positionChanged || sizeChanged)
        SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!layoutValid_ || sizeChanged) onLayout(width, height);
    layoutX_ = x;
    layoutY_ = y;
    layoutWidth_ = width;
    layoutHeight_ = height;
    layoutValid_ = true;
}

void NativePage::applyAppearance(const Theme& theme, float uiScale) {
    theme_ = theme;
    uiScale_ = std::clamp(uiScale, 0.8f, 1.5f);
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    if (panelBrush_) DeleteObject(panelBrush_);
    if (editBrush_) DeleteObject(editBrush_);
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    panelBrush_ = CreateSolidBrush(theme_.panel);
    editBrush_ = CreateSolidBrush(theme_.page);

    if (uiFont_) DeleteObject(uiFont_);
    if (uiFontBold_) DeleteObject(uiFontBold_);
    const int fontHeight = -std::max(12, static_cast<int>(std::lround(16.0f * uiScale_)));
    uiFont_ = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    uiFontBold_ = CreateFontW(fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (HWND control : controls_) {
        const bool bold = std::find(boldControls_.begin(), boldControls_.end(), control) != boldControls_.end();
        SetFont(control, bold ? uiFontBold_ : uiFont_);
        if (GetPropW(control,L"Mezozoy.Ui.List")) SendMessageW(control,LB_SETITEMHEIGHT,0,static_cast<int>(style::RowHeight*uiScale_));
        InvalidateRect(control, nullptr, TRUE);
    }
    layoutValid_ = false;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void NativePage::show(bool visible) { ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE); }

HBRUSH NativePage::controlColor(HDC dc, HWND control, UINT message) {
    if (message == WM_CTLCOLORLISTBOX && GetPropW(control, L"Mezozoy.Ui.List")) {
        SetTextColor(dc, theme_.text); SetBkColor(dc, theme_.navigation);
        SetDCBrushColor(dc, theme_.navigation);
        return reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    }
    SetTextColor(dc, theme_.text); SetBkColor(dc, message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ? theme_.page : theme_.background);
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) return editBrush_;
    if (message == WM_CTLCOLORBTN) { SetBkMode(dc, TRANSPARENT); return panelBrush_; }
    SetBkMode(dc, TRANSPARENT); return backgroundBrush_;
}

void NativePage::drawButton(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON) return;
    style::Button(draw, theme_);
}

LRESULT CALLBACK NativePage::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativePage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<NativePage*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_ERASEBKGND) { RECT r{}; GetClientRect(window, &r); FillRect(reinterpret_cast<HDC>(wParam), &r, self->backgroundBrush_); return 1; }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORBTN)
        return reinterpret_cast<LRESULT>(self->controlColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam), message));
    if (message == WM_COMMAND) { if (self->handleCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0; }
    if (message == WM_NOTIFY) {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header && header->code == NM_CUSTOMDRAW) {
            wchar_t cls[32]{}; GetClassNameW(header->hwndFrom,cls,32);
            if (_wcsicmp(cls,WC_TREEVIEWW)==0)
                return style::Tree(reinterpret_cast<NMTVCUSTOMDRAW*>(header),self->theme_,self->uiFont_,self->uiFontBold_);
        }
        if (self->handleNotify(header)) return header && header->code == EN_PROTECTED ? TRUE : 0;
    }
    if (message == WM_DRAWITEM) {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw && draw->CtlType==ODT_LISTBOX && GetPropW(draw->hwndItem,L"Mezozoy.Ui.List")) {
            style::ListRow(draw,self->theme_); return TRUE;
        }
        if (!self->drawCustomItem(draw)) self->drawButton(draw);
        return TRUE;
    }
    const LRESULT custom = self->onMessage(message, wParam, lParam);
    if (custom) return custom;
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace mezozoy::ui
