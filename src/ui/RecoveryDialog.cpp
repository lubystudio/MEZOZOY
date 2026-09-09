#include "RecoveryDialog.h"

#include "Win32Util.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace mezozoy::ui {
namespace {

enum : int {
    IdCandidateList = 7301,
    IdDetails = 7302,
    IdOpenOriginal = 7303,
    IdOpenFolder = 7304
};

struct RecoveryDialogState {
    RecoveryScan scan;
    Theme theme;
    RecoveryDialogResult result;
    HWND list{};
    HWND details{};
    HWND primary{};
    HWND original{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HBRUSH backgroundBrush{};
    HBRUSH panelBrush{};
};

std::wstring FormatDate(std::filesystem::file_time_type value) {
    if (value == std::filesystem::file_time_type{}) return L"Время неизвестно";
    const auto systemValue = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        value - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(systemValue);
    std::tm local{};
    localtime_s(&local, &time);
    std::wostringstream output;
    output << std::put_time(&local, L"%d.%m.%Y  %H:%M:%S");
    return output.str();
}

std::wstring FormatSize(std::uintmax_t bytes) {
    std::wostringstream output;
    if (bytes >= 1024 * 1024) {
        output << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / (1024.0 * 1024.0) << L" МБ";
    } else if (bytes >= 1024) {
        output << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024.0 << L" КБ";
    } else {
        output << bytes << L" Б";
    }
    return output.str();
}

int SelectedIndex(const RecoveryDialogState& state) {
    if (!state.list) return -1;
    return static_cast<int>(SendMessageW(state.list, LB_GETCURSEL, 0, 0));
}

void UpdateSelection(RecoveryDialogState& state) {
    const int index = SelectedIndex(state);
    if (index < 0 || index >= static_cast<int>(state.scan.candidates.size())) return;
    const RecoveryCandidate& candidate = state.scan.candidates[static_cast<std::size_t>(index)];
    std::wstring text = L"Проект: " + (candidate.projectTitle.empty() ? L"Не удалось прочитать" : candidate.projectTitle) +
                        L"\r\nФайл: " + candidate.path.filename().wstring() + L"\r\nИзменен: " + FormatDate(candidate.modified) +
                        L"    Размер: " + FormatSize(candidate.size) + L"\r\n";
    text += candidate.valid ? L"Проверка пройдена. Эту копию можно безопасно открыть."
                            : L"Копия повреждена: " + candidate.error;
    SetWindowTextW(state.details, text.c_str());
    EnableWindow(state.primary, candidate.valid ? TRUE : FALSE);
    SetWindowTextW(state.primary, candidate.source == RecoverySource::Original ? L"Открыть выбранное" : L"Восстановить и открыть");
}

void AcceptCandidate(HWND window, RecoveryDialogState& state, std::size_t index) {
    if (index >= state.scan.candidates.size()) return;
    const RecoveryCandidate& candidate = state.scan.candidates[index];
    if (!candidate.valid) { MessageBeep(MB_ICONWARNING); return; }
    state.result.accepted = true;
    state.result.recovered = candidate.source != RecoverySource::Original;
    state.result.selectedPath = candidate.path;
    DestroyWindow(window);
}

void DrawCandidate(const DRAWITEMSTRUCT& draw, const RecoveryDialogState& state) {
    if (draw.itemID == static_cast<UINT>(-1) || draw.itemID >= state.scan.candidates.size()) return;
    const RecoveryCandidate& candidate = state.scan.candidates[draw.itemID];
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    HBRUSH background = CreateSolidBrush(selected ? state.theme.selection : state.theme.panelAlt);
    FillRect(draw.hDC, &draw.rcItem, background);
    DeleteObject(background);

    RECT accent = draw.rcItem;
    accent.right = accent.left + (draw.itemID == state.scan.recommendedIndex ? 4 : 1);
    HBRUSH accentBrush = CreateSolidBrush(draw.itemID == state.scan.recommendedIndex ? state.theme.accent : state.theme.border);
    FillRect(draw.hDC, &accent, accentBrush);
    DeleteObject(accentBrush);

    SetBkMode(draw.hDC, TRANSPARENT);
    RECT title = draw.rcItem;
    title.left += 16; title.top += 8; title.right -= 12; title.bottom = title.top + 22;
    SelectObject(draw.hDC, state.bodyFont);
    SetTextColor(draw.hDC, state.theme.text);
    std::wstring heading = RecoveryService::SourceLabel(candidate.source) + L"  ·  " + candidate.path.filename().wstring();
    DrawTextW(draw.hDC, heading.c_str(), -1, &title, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT info = draw.rcItem;
    info.left += 16; info.top += 34; info.right -= 150; info.bottom = info.top + 20;
    SelectObject(draw.hDC, state.smallFont);
    SetTextColor(draw.hDC, state.theme.textMuted);
    const std::wstring metadata = FormatDate(candidate.modified) + L"    " + FormatSize(candidate.size);
    DrawTextW(draw.hDC, metadata.c_str(), -1, &info, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT status = draw.rcItem;
    status.left = status.right - 138; status.top += 34; status.right -= 12; status.bottom = status.top + 20;
    SetTextColor(draw.hDC, candidate.valid ? state.theme.accent : state.theme.danger);
    const wchar_t* statusText = candidate.valid ? L"Готово к открытию" : L"Повреждено";
    DrawTextW(draw.hDC, statusText, -1, &status, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
}

LRESULT CALLBACK RecoveryProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<RecoveryDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<RecoveryDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        state->titleFont = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        state->bodyFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        state->smallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        state->backgroundBrush = CreateSolidBrush(state->theme.background);
        state->panelBrush = CreateSolidBrush(state->theme.panelAlt);

        BOOL dark = !state->theme.light;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        HWND title = CreateChild(L"STATIC", L"Восстановление проекта", SS_LEFT, 7310, window, create->hInstance);
        const wchar_t* explanation = state->scan.originalValid
            ? L"Найдена более свежая автоматически сохраненная версия. Выберите, какую копию открыть."
            : L"Исходный файл поврежден, но Mezozoy нашла рабочую копию. Исходник не будет перезаписан без вашего Ctrl+S.";
        HWND subtitle = CreateChild(L"STATIC", explanation, SS_LEFT, 7311, window, create->hInstance);
        state->list = CreateChild(L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER,
                                  IdCandidateList, window, create->hInstance, WS_EX_CLIENTEDGE);
        state->details = CreateChild(L"STATIC", L"", SS_LEFT, IdDetails, window, create->hInstance);
        state->primary = CreateChild(L"BUTTON", L"Восстановить и открыть", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, window, create->hInstance);
        state->original = CreateChild(L"BUTTON", L"Открыть исходный", BS_PUSHBUTTON | WS_TABSTOP, IdOpenOriginal, window, create->hInstance);
        HWND folder = CreateChild(L"BUTTON", L"Открыть папку", BS_PUSHBUTTON | WS_TABSTOP, IdOpenFolder, window, create->hInstance);
        HWND cancel = CreateChild(L"BUTTON", L"Отмена", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, window, create->hInstance);

        SetFont(title, state->titleFont);
        for (HWND item : {subtitle, state->list, state->details, state->primary, state->original, folder, cancel}) {
            SetFont(item, state->bodyFont);
            SetWindowTheme(item, state->theme.light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        }

        SetWindowPos(title, nullptr, 24, 20, 700, 32, SWP_NOZORDER);
        SetWindowPos(subtitle, nullptr, 24, 58, 700, 42, SWP_NOZORDER);
        SetWindowPos(state->list, nullptr, 24, 108, 712, 224, SWP_NOZORDER);
        SetWindowPos(state->details, nullptr, 24, 348, 712, 80, SWP_NOZORDER);
        SetWindowPos(folder, nullptr, 24, 450, 116, 34, SWP_NOZORDER);
        SetWindowPos(state->original, nullptr, 278, 450, 140, 34, SWP_NOZORDER);
        SetWindowPos(cancel, nullptr, 426, 450, 100, 34, SWP_NOZORDER);
        SetWindowPos(state->primary, nullptr, 534, 450, 202, 34, SWP_NOZORDER);

        SendMessageW(state->list, LB_SETITEMHEIGHT, 0, 64);
        for (std::size_t index = 0; index < state->scan.candidates.size(); ++index) {
            const int row = static_cast<int>(SendMessageW(state->list, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(RecoveryService::SourceLabel(state->scan.candidates[index].source).c_str())));
            SendMessageW(state->list, LB_SETITEMDATA, row, static_cast<LPARAM>(index));
        }
        const std::size_t selected = std::min(state->scan.recommendedIndex, state->scan.candidates.size() - 1);
        SendMessageW(state->list, LB_SETCURSEL, selected, 0);
        EnableWindow(state->original, state->scan.originalValid ? TRUE : FALSE);
        UpdateSelection(*state);
        SetFocus(state->list);
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
    if (state && message == WM_CTLCOLORLISTBOX) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, state->theme.text); SetBkColor(dc, state->theme.panelAlt);
        return reinterpret_cast<LRESULT>(state->panelBrush);
    }
    if (state && message == WM_DRAWITEM) {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw && draw->CtlID == IdCandidateList) { DrawCandidate(*draw, *state); return TRUE; }
    }
    if (state && message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == IdCandidateList && notification == LBN_SELCHANGE) { UpdateSelection(*state); return 0; }
        if (id == IdCandidateList && notification == LBN_DBLCLK) {
            const int index = SelectedIndex(*state);
            if (index >= 0) AcceptCandidate(window, *state, static_cast<std::size_t>(index));
            return 0;
        }
        if (id == IDOK) {
            const int index = SelectedIndex(*state);
            if (index >= 0) AcceptCandidate(window, *state, static_cast<std::size_t>(index));
            return 0;
        }
        if (id == IdOpenOriginal) { AcceptCandidate(window, *state, 0); return 0; }
        if (id == IdOpenFolder) {
            ShellExecuteW(window, L"open", L"explorer.exe", state->scan.originalPath.parent_path().c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == IDCANCEL) { DestroyWindow(window); return 0; }
    }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) {
        if (state) {
            if (state->titleFont) DeleteObject(state->titleFont);
            if (state->bodyFont) DeleteObject(state->bodyFont);
            if (state->smallFont) DeleteObject(state->smallFont);
            if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
            if (state->panelBrush) DeleteObject(state->panelBrush);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

RecoveryDialogResult ShowRecoveryDialog(HWND owner, const RecoveryScan& scan, const Theme& theme) {
    static bool registered = false;
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = RecoveryProc; wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr; wc.lpszClassName = L"Mezozoy.RecoveryDialog";
        RegisterClassExW(&wc); registered = true;
    }

    RecoveryDialogState state;
    state.scan = scan;
    state.theme = theme;
    EnableWindow(owner, FALSE);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"Mezozoy.RecoveryDialog", L"Mezozoy — восстановление проекта",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 776, 540,
                                  owner, nullptr, instance, &state);
    if (!dialog) {
        EnableWindow(owner, TRUE);
        return {};
    }
    CenterWindow(dialog, owner); ShowWindow(dialog, SW_SHOW); UpdateWindow(dialog);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return state.result;
}

}  // namespace mezozoy::ui
