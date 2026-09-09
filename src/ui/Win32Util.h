#pragma once

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <filesystem>
#include <string>

namespace mezozoy::ui {

// Native controls repaint immediately when they are shown, hidden, or moved.  Complex
// editor switches touch dozens of controls, so without a short transaction Windows can
// present several half-built frames.  This lock is deliberately local to one UI update;
// it is never held across the message loop or while the top-level window is being moved.
class ScopedRedrawLock {
public:
    explicit ScopedRedrawLock(
        HWND window,
        UINT redrawFlags = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW)
        : window_(IsWindow(window) ? window : nullptr), redrawFlags_(redrawFlags) {
        if (!window_) return;
        const ULONG_PTR depth = reinterpret_cast<ULONG_PTR>(GetPropW(window_, PropertyName));
        SetPropW(window_, PropertyName, reinterpret_cast<HANDLE>(depth + 1));
        if (depth == 0) SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    }

    ScopedRedrawLock(const ScopedRedrawLock&) = delete;
    ScopedRedrawLock& operator=(const ScopedRedrawLock&) = delete;

    ~ScopedRedrawLock() { release(); }

    void release(bool repaint = true) {
        HWND window = window_;
        window_ = nullptr;
        if (!IsWindow(window)) return;

        const ULONG_PTR depth = reinterpret_cast<ULONG_PTR>(GetPropW(window, PropertyName));
        if (depth > 1) {
            SetPropW(window, PropertyName, reinterpret_cast<HANDLE>(depth - 1));
            return;
        }

        RemovePropW(window, PropertyName);
        SendMessageW(window, WM_SETREDRAW, TRUE, 0);
        if (repaint && IsWindowVisible(window)) RedrawWindow(window, nullptr, nullptr, redrawFlags_);
    }

private:
    static constexpr wchar_t PropertyName[] = L"Mezozoy.ScopedRedrawDepth";
    HWND window_ = nullptr;
    UINT redrawFlags_ = 0;
};

template <typename T>
void SafeRelease(T*& value) {
    if (value) { value->Release(); value = nullptr; }
}

inline std::wstring WindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

inline void SetFont(HWND window, HFONT font, bool redraw = true) {
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), redraw ? TRUE : FALSE);
}

inline HWND CreateChild(const wchar_t* className, const wchar_t* text, DWORD style, int id, HWND parent, HINSTANCE instance,
                        DWORD exStyle = 0) {
    return CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
}

inline bool IsTextInput(HWND window) {
    if (!window) return false;
    wchar_t name[64]{};
    GetClassNameW(window, name, 63);
    return _wcsicmp(name, L"Edit") == 0 || _wcsicmp(name, L"RICHEDIT50W") == 0 || _wcsicmp(name, L"RichEdit20W") == 0;
}

inline void CenterWindow(HWND window, HWND owner = nullptr) {
    RECT area{};
    if (owner && IsWindow(owner)) GetWindowRect(owner, &area); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    RECT rect{}; GetWindowRect(window, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    SetWindowPos(window, nullptr, area.left + ((area.right - area.left) - width) / 2,
                 area.top + ((area.bottom - area.top) - height) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

inline std::filesystem::path OpenFileDialog(HWND owner, bool save, const wchar_t* filter, const wchar_t* defaultExtension,
                                            const std::wstring& suggested = {}) {
    wchar_t buffer[32768]{};
    if (!suggested.empty()) wcsncpy_s(buffer, suggested.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrDefExt = defaultExtension;
    dialog.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    return accepted ? std::filesystem::path(buffer) : std::filesystem::path{};
}

}  // namespace mezozoy::ui
