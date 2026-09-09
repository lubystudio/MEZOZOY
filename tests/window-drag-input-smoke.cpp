#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t MainClass[] = L"Mezozoy.MainWindow";
constexpr UINT QueryMoveState = WM_APP + 91;
constexpr UINT QueryAccent = WM_APP + 92;
constexpr UINT QuerySelection = WM_APP + 93;

struct FindWindowContext {
    DWORD processId{};
    HWND result{};
};

BOOL CALLBACK FindMainWindow(HWND window, LPARAM value) {
    auto& context = *reinterpret_cast<FindWindowContext*>(value);
    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    if (processId != context.processId || !IsWindowVisible(window)) return TRUE;
    wchar_t className[128]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    if (wcscmp(className, MainClass) != 0) return TRUE;
    context.result = window;
    return FALSE;
}

HWND WaitForMainWindow(DWORD processId) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        FindWindowContext context{processId};
        EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&context));
        if (context.result) return context.result;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    return nullptr;
}

BOOL CALLBACK FindChildById(HWND child, LPARAM value) {
    auto& pair = *reinterpret_cast<std::pair<int, HWND>*>(value);
    if (GetDlgCtrlID(child) != pair.first) return TRUE;
    pair.second = child;
    return FALSE;
}

HWND Descendant(HWND root, int id) {
    std::pair<int, HWND> search{id, nullptr};
    EnumChildWindows(root, FindChildById, reinterpret_cast<LPARAM>(&search));
    return search.second;
}

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM value) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        auto& monitors = *reinterpret_cast<std::vector<MONITORINFO>*>(value);
        if (info.dwFlags & MONITORINFOF_PRIMARY) monitors.insert(monitors.begin(), info);
        else monitors.push_back(info);
    }
    return TRUE;
}

std::wstring Hex(COLORREF color) {
    wchar_t value[16]{};
    swprintf_s(value, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return value;
}

bool Responsive(HWND window) {
    DWORD_PTR result{};
    return SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1200, &result) != 0;
}

double DragWindow(HWND window, const RECT& destination, bool& enteredMoveLoop) {
    RECT start{};
    GetWindowRect(window, &start);
    const int width = start.right - start.left;
    const int anchorX = std::clamp(width / 2, 160, std::max(160, width - 160));
    std::optional<int> captionY;
    const int scanLimit = std::min(90, static_cast<int>(start.bottom - start.top));
    for (int y = 2; y < scanLimit; ++y) {
        const POINT candidate{start.left + anchorX, start.top + y};
        const LRESULT hit = SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(candidate.x, candidate.y));
        if (hit == HTCAPTION) {
            captionY = y;
            break;
        }
    }
    if (!captionY) {
        std::wcerr << L"No draggable caption point was found\n";
        return 10000.0;
    }
    const int anchorY = *captionY;
    const POINT from{start.left + anchorX, start.top + anchorY};
    const POINT to{
        destination.left + std::max(170L, (destination.right - destination.left) / 2),
        destination.top + 100
    };

    SetForegroundWindow(window);
    SetCursorPos(from.x, from.y);
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    // Run the real system command on a helper thread. A synchronous call enters
    // DefWindowProc's modal move loop; this thread remains free to provide the
    // same physical cursor stream a user would produce.
    std::thread systemMove([window, from] {
        SendMessageW(window, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, MAKELPARAM(from.x, from.y));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    double maximumAnchorError = 0.0;
    constexpr int Steps = 42;
    for (int step = 1; step <= Steps; ++step) {
        const double t = static_cast<double>(step) / Steps;
        const int x = static_cast<int>(std::lround(from.x + (to.x - from.x) * t));
        const int y = static_cast<int>(std::lround(from.y + (to.y - from.y) * t));
        SetCursorPos(x, y);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        const LRESULT state = SendMessageW(window, QueryMoveState, 0, 0);
        enteredMoveLoop = enteredMoveLoop || (state & 1) != 0;
        RECT current{};
        GetWindowRect(window, &current);
        const double errorX = static_cast<double>((x - current.left) - anchorX);
        const double errorY = static_cast<double>((y - current.top) - anchorY);
        maximumAnchorError = std::max(maximumAnchorError, std::hypot(errorX, errorY));
    }

    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    if (systemMove.joinable()) systemMove.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    return maximumAnchorError;
}

void RestoreAccent(HWND window, COLORREF color) {
    constexpr COLORREF palette[] = {
        RGB(139, 92, 246), RGB(236, 72, 153), RGB(168, 85, 247), RGB(79, 70, 229),
        RGB(14, 165, 233), RGB(20, 184, 166), RGB(34, 197, 94), RGB(249, 115, 22)
    };
    for (int index = 0; index < static_cast<int>(std::size(palette)); ++index) {
        if (palette[index] != color) continue;
        if (HWND swatch = Descendant(window, 5300 + index)) {
            SendMessageW(swatch, BM_CLICK, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(220));
            return;
        }
    }
    HWND edit = Descendant(window, 5104);
    if (!edit) return;
    const std::wstring value = Hex(color);
    SetWindowTextW(edit, value.c_str());
    SendMessageW(GetParent(edit), WM_COMMAND, MAKEWPARAM(5104, EN_KILLFOCUS), reinterpret_cast<LPARAM>(edit));
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::wcerr << L"Usage: window-drag-input-smoke.exe <Mezozoy.exe> <project.mzoy>\n";
        return 2;
    }

    const std::filesystem::path executable = std::filesystem::absolute(argv[1]);
    const std::filesystem::path project = std::filesystem::absolute(argv[2]);
    const bool accentOnly = argc > 3 && std::wstring(argv[3]) == L"--accent-only";
    std::wstring command = L"\"" + executable.wstring() + L"\" \"" + project.wstring() + L"\"";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), commandBuffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        executable.parent_path().c_str(), &startup, &process)) {
        std::wcerr << L"CreateProcess failed: " << GetLastError() << L"\n";
        return 3;
    }

    int result = 0;
    HWND window = WaitForMainWindow(process.dwProcessId);
    if (!window) {
        std::wcerr << L"Main window was not found\n";
        result = 4;
    } else {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));

        const COLORREF originalAccent = static_cast<COLORREF>(SendMessageW(window, QueryAccent, 0, 0));
        const COLORREF originalSelection = static_cast<COLORREF>(SendMessageW(window, QuerySelection, 0, 0));
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(6110, BN_CLICKED), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
        const COLORREF testAccent = originalAccent == RGB(34, 197, 94) ? RGB(249, 115, 22) : RGB(34, 197, 94);
        const int accentIndex = testAccent == RGB(34, 197, 94) ? 6 : 7;
        HWND swatch = Descendant(window, 5300 + accentIndex);
        if (!swatch) {
            std::wcerr << L"Accent swatch was not found\n";
            result = 5;
        } else {
            SendMessageW(swatch, BM_CLICK, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(220));
            const COLORREF appliedAccent = static_cast<COLORREF>(SendMessageW(window, QueryAccent, 0, 0));
            const COLORREF appliedSelection = static_cast<COLORREF>(SendMessageW(window, QuerySelection, 0, 0));
            if (appliedAccent != testAccent || appliedSelection == originalSelection) {
                std::wcerr << L"Accent did not propagate. requested=" << Hex(testAccent)
                           << L" applied=" << Hex(appliedAccent)
                           << L" selection=" << Hex(appliedSelection) << L"\n";
                result = 6;
            }
        }
        RestoreAccent(window, originalAccent);
        if (static_cast<COLORREF>(SendMessageW(window, QueryAccent, 0, 0)) != originalAccent) {
            std::wcerr << L"Original accent was not restored\n";
            result = 7;
        }

        std::vector<MONITORINFO> monitors;
        if (!accentOnly) {
            EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
        }
        if (!accentOnly && monitors.empty()) {
            std::wcerr << L"No monitors reported\n";
            result = 8;
        } else if (!accentOnly) {
            const RECT primary = monitors.front().rcWork;
            const int initialWidth = std::min(1180L, primary.right - primary.left - 100);
            const int initialHeight = std::min(760L, primary.bottom - primary.top - 100);
            double maximumError = 0.0;
            for (const int page : {6104, 6105, 6110}) {
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(page, BN_CLICKED), 0);
                SetWindowPos(window, nullptr, primary.left + 50, primary.top + 50,
                             initialWidth, initialHeight, SWP_NOZORDER | SWP_NOACTIVATE);
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                const RECT target = monitors.size() > 1 ? monitors[1].rcWork : primary;
                bool enteredMoveLoop = false;
                maximumError = std::max(maximumError, DragWindow(window, target, enteredMoveLoop));
                const LRESULT finalState = SendMessageW(window, QueryMoveState, 0, 0);
                if (!enteredMoveLoop || finalState != 0 || !Responsive(window)) {
                    std::wcerr << L"Move state failed on page " << page << L": entered=" << enteredMoveLoop
                               << L" finalState=" << finalState << L"\n";
                    result = 9;
                    break;
                }
            }
            if (maximumError > 140.0) {
                std::wcerr << L"Window lagged behind the physical cursor. maxAnchorError=" << maximumError << L" px\n";
                result = 10;
            }
            std::wcout << L"WINDOW_DRAG_INPUT displays=" << monitors.size()
                       << L" maxAnchorErrorPx=" << maximumError << L"\n";
        }
    }

    if (window) PostMessageW(window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process.hProcess, 3000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 0);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (result == 0) {
        if (accentOnly) std::wcout << L"ACCENT_PROPAGATION_SMOKE_OK\n";
        else std::wcout << L"WINDOW_DRAG_INPUT_SMOKE_OK\n";
    }
    return result;
}
