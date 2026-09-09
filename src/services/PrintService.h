#pragma once

#include "ScreenplayLayoutService.h"
#include "../core/Models.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace mezozoy {

class PrintService {
public:
    void showPreview(HWND owner, const Project& project,
                     ScreenplayPaper paper = ScreenplayPaper::HollywoodLetter);
    bool exportPdf(HWND owner, const Project& project, const std::filesystem::path& file,
                   ScreenplayPaper paper = ScreenplayPaper::HollywoodLetter,
                   std::wstring* error = nullptr);
    bool print(HWND owner, const Project& project,
               ScreenplayPaper paper = ScreenplayPaper::HollywoodLetter,
               std::wstring* error = nullptr);
    void drawPage(HDC dc, const RECT& page, const ScreenplayLayoutPage& layoutPage,
                  const ScreenplayLayoutDocument& document, const Project& project,
                  bool preview) const;

private:
    static std::wstring pdfPrinter();
};

}  // namespace mezozoy
