#include <windows.h>
#include <gdiplus.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct SearchContext {
    std::wstring titlePart;
    HWND found{};
};

BOOL CALLBACK FindWindowCallback(HWND window, LPARAM value) {
    auto* context = reinterpret_cast<SearchContext*>(value);
    if (!IsWindowVisible(window)) return TRUE;

    const int length = GetWindowTextLengthW(window);
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<size_t>(length));
    if (title.find(context->titlePart) == std::wstring::npos) return TRUE;

    context->found = window;
    return FALSE;
}

int PngEncoderClsid(CLSID* result) {
    UINT count = 0;
    UINT bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (!bytes) return -1;

    auto* encoders = static_cast<Gdiplus::ImageCodecInfo*>(std::malloc(bytes));
    if (!encoders) return -1;
    Gdiplus::GetImageEncoders(count, bytes, encoders);
    for (UINT index = 0; index < count; ++index) {
        if (wcscmp(encoders[index].MimeType, L"image/png") == 0) {
            *result = encoders[index].Clsid;
            std::free(encoders);
            return 0;
        }
    }
    std::free(encoders);
    return -1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"Usage: MezozoyCaptureWindow <title-part> <output.png>\n";
        return 2;
    }

    SearchContext context{argv[1]};
    EnumWindows(FindWindowCallback, reinterpret_cast<LPARAM>(&context));
    if (!context.found) {
        std::wcerr << L"Window was not found: " << argv[1] << L"\n";
        return 3;
    }

    RECT rect{};
    if (!GetWindowRect(context.found, &rect)) return 4;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return 5;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    PatBlt(memory, 0, 0, width, height, BLACKNESS);
    if (!PrintWindow(context.found, memory, 2)) {
        BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
    }
    SelectObject(memory, previous);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) != Gdiplus::Ok) {
        DeleteObject(bitmap);
        return 6;
    }

    const std::filesystem::path output = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(output.parent_path());
    CLSID png{};
    const int encoderResult = PngEncoderClsid(&png);
    Gdiplus::Bitmap image(bitmap, nullptr);
    const auto saveResult = encoderResult == 0 ? image.Save(output.c_str(), &png, nullptr) : Gdiplus::GenericError;
    Gdiplus::GdiplusShutdown(token);
    DeleteObject(bitmap);
    return saveResult == Gdiplus::Ok ? 0 : 7;
}
