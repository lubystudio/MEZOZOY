#include "Dialogs.h"

#include "Theme.h"
#include "Win32Util.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace mezozoy::ui {
namespace {

struct PromptState {
    std::wstring label;
    std::wstring value;
    bool accepted = false;
    HWND edit{};
    HFONT font{};
    Theme theme{};
    HBRUSH backgroundBrush{};
    HBRUSH editBrush{};
};

LRESULT CALLBACK PromptProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<PromptState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        state->backgroundBrush = CreateSolidBrush(state->theme.panel);
        state->editBrush = CreateSolidBrush(state->theme.page);
        BOOL dark = !state->theme.light;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        HWND label = CreateChild(L"STATIC", state->label.c_str(), SS_LEFT, 1001, window, create->hInstance);
        state->edit = CreateChild(L"EDIT", state->value.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 1002, window, create->hInstance, WS_EX_CLIENTEDGE);
        HWND ok = CreateChild(L"BUTTON", L"ОК", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, window, create->hInstance);
        HWND cancel = CreateChild(L"BUTTON", L"Отмена", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, window, create->hInstance);
        for (HWND item : {label, state->edit, ok, cancel}) {
            SetFont(item, state->font);
            SetWindowTheme(item, state->theme.light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        }
        SetWindowPos(label, nullptr, 18, 18, 424, 22, SWP_NOZORDER);
        SetWindowPos(state->edit, nullptr, 18, 44, 424, 30, SWP_NOZORDER);
        SetWindowPos(ok, nullptr, 266, 90, 84, 30, SWP_NOZORDER);
        SetWindowPos(cancel, nullptr, 358, 90, 84, 30, SWP_NOZORDER);
        SetFocus(state->edit); SendMessageW(state->edit, EM_SETSEL, 0, -1);
        return 0;
    }
    if (state && message == WM_ERASEBKGND) {
        RECT rect{}; GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, state->backgroundBrush);
        return 1;
    }
    if (state && (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN)) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, state->theme.text); SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(state->backgroundBrush);
    }
    if (state && message == WM_CTLCOLOREDIT) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, state->theme.text); SetBkColor(dc, state->theme.page);
        return reinterpret_cast<LRESULT>(state->editBrush);
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        // Edit notifications must never be mistaken for OK/Cancel commands.
        if (HIWORD(wParam) == BN_CLICKED && (id == IDOK || id == IDCANCEL)) {
            if (id == IDOK) { state->value = WindowText(state->edit); state->accepted = true; }
            DestroyWindow(window); return 0;
        }
    }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) {
        if (state) {
            if (state->font) DeleteObject(state->font);
            if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
            if (state->editBrush) DeleteObject(state->editBrush);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

std::wstring PromptText(HWND owner, const std::wstring& title, const std::wstring& label, const std::wstring& initial, const Theme& theme) {
    static bool registered = false;
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PromptProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; wc.lpszClassName = L"Mezozoy.Prompt";
        RegisterClassExW(&wc); registered = true;
    }
    PromptState state{label, initial};
    state.theme = theme;
    EnableWindow(owner, FALSE);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"Mezozoy.Prompt", title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 476, 166, owner, nullptr, instance, &state);
    CenterWindow(dialog, owner); ShowWindow(dialog, SW_SHOW); UpdateWindow(dialog);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return state.accepted ? state.value : std::wstring{};
}

}  // namespace mezozoy::ui
