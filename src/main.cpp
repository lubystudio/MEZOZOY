#include "ui/MainWindow.h"
#include "services/CrashReporter.h"
#include "services/SelfTest.h"
#include "core/AppVersion.h"
#include "core/Utf.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    mezozoy::CrashReporter::Install();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    try {
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc > 1 && std::wstring(argv[1]) == L"--self-test") {
            const std::filesystem::path compatibility = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path{};
            LocalFree(argv);
            const int result = mezozoy::RunSelfTests(compatibility);
            if (SUCCEEDED(com)) CoUninitialize();
            return result;
        }
        std::filesystem::path startup;
        if (argv && argc > 1) startup = argv[1]; if (argv) LocalFree(argv);
        mezozoy::ui::MainWindow window(instance);
        if (!window.create(startup)) {
            MessageBoxW(nullptr, L"Не удалось создать главное окно Mezozoy.", L"Mezozoy", MB_ICONERROR);
            if (SUCCEEDED(com)) CoUninitialize(); return 1;
        }
        const int result = window.run(showCommand);
        if (SUCCEEDED(com)) CoUninitialize(); return result;
    } catch (const std::exception& ex) {
        const std::wstring message = mezozoy::utf::FromUtf8(ex.what());
        mezozoy::CrashReporter::Log(L"Необработанное исключение C++", message);
        MessageBoxW(nullptr, (L"Mezozoy аварийно завершилась.\n\n" + message + L"\n\nКраш-лог: " + mezozoy::CrashReporter::LogPath().wstring()).c_str(), mezozoy::AppNameAndVersion, MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize(); return 2;
    } catch (...) {
        mezozoy::CrashReporter::Log(L"Неизвестное исключение C++", L"Тип исключения недоступен.");
        MessageBoxW(nullptr, L"Mezozoy аварийно завершилась. Подробности записаны в краш-лог.", mezozoy::AppNameAndVersion, MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize(); return 3;
    }
}
