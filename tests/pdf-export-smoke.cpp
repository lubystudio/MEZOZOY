#include "services/ProjectSerializer.h"
#include "services/PrintService.h"
#include <iostream>
#include <objbase.h>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    mezozoy::Project project;
    std::wstring error;
    if (!mezozoy::ProjectSerializer{}.load(argv[1], project, &error)) {
        std::wcerr << error << L'\n'; return 3;
    }
    for (auto paper : {mezozoy::ScreenplayPaper::HollywoodLetter, mezozoy::ScreenplayPaper::A4}) {
        const auto name = paper == mezozoy::ScreenplayPaper::A4 ? L"Horizon-A4.pdf" : L"Horizon-Letter.pdf";
        const auto output = std::filesystem::absolute(std::filesystem::path(argv[2]) / name);
        if (!mezozoy::PrintService{}.exportPdf(nullptr, project, output, paper, &error)) {
            std::wcerr << error << L'\n'; return 4;
        }
        const auto layout = mezozoy::ScreenplayLayoutService{}.layout(project, paper, true);
        std::wcout << name << L": " << layout.pages.size() << L" pages including title\n";
    }
    CoUninitialize();
    return 0;
}
