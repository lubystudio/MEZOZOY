#include "CrashReporter.h"

#include "../core/Utf.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace mezozoy {
namespace {

LONG WINAPI SehHandler(EXCEPTION_POINTERS* information) {
    std::wostringstream details;
    details << L"SEH 0x" << std::hex << std::uppercase
            << (information && information->ExceptionRecord ? information->ExceptionRecord->ExceptionCode : 0)
            << L"; address="
            << (information && information->ExceptionRecord ? information->ExceptionRecord->ExceptionAddress : nullptr);
    CrashReporter::Log(L"Необработанное системное исключение", details.str());
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

std::filesystem::path CrashReporter::LogPath() {
    wchar_t documents[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, documents))) {
        return std::filesystem::path(documents) / L"Mezozoy" / L"Mezozoy-crash.log";
    }
    return std::filesystem::current_path() / L"Mezozoy-crash.log";
}

void CrashReporter::Install() {
    SetUnhandledExceptionFilter(SehHandler);
    std::set_terminate([]() {
        CrashReporter::Log(L"std::terminate", L"Приложение было аварийно остановлено библиотекой C++.");
        TerminateProcess(GetCurrentProcess(), 3);
    });
}

void CrashReporter::Log(const std::wstring& context, const std::wstring& details) {
    try {
        const auto file = LogPath();
        std::filesystem::create_directories(file.parent_path());
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::wostringstream message;
        message << L"[" << std::setfill(L'0') << std::setw(4) << time.wYear << L'-' << std::setw(2) << time.wMonth << L'-' << std::setw(2) << time.wDay
                << L' ' << std::setw(2) << time.wHour << L':' << std::setw(2) << time.wMinute << L':' << std::setw(2) << time.wSecond << L"] "
                << context << L"\n" << details << L"\n\n";

        const std::string bytes = utf::ToUtf8(message.str());
        std::ofstream out(file, std::ios::binary | std::ios::app);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    } catch (...) {
    }
}

}  // namespace mezozoy
