#include "PrintService.h"

#include "../core/Utf.h"
#include "../ui/Win32Util.h"

#include <windowsx.h>
#include <winspool.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace mezozoy {
namespace {

struct PreviewState {
    PrintService* service{};
    Project project;
    ScreenplayPaper paper = ScreenplayPaper::HollywoodLetter;
    ScreenplayLayoutDocument document;
    int scroll = 0;
    float zoom = 0.82f;
    HWND owner{};
    HWND window{};
    HWND exportButton{};
    HWND printButton{};
    HWND letterButton{};
    HWND a4Button{};
    HFONT font{};
};

struct PreviewGeometry {
    int pageWidth = 1;
    int pageHeight = 1;
    int gap = 28;
    int top = 64;
    int totalHeight = 1;
};

constexpr wchar_t PreviewClass[] = L"Mezozoy.PrintPreview";
constexpr int ExportButton = 9901;
constexpr int LetterButton = 9902;
constexpr int A4Button = 9903;
constexpr int PrintButton = 9904;

PreviewGeometry Geometry(const PreviewState& state) {
    PreviewGeometry result;
    const ScreenplayPageSpec& spec = state.document.spec;
    result.pageWidth = std::max(1, static_cast<int>(std::lround(spec.widthInches * 96.0 * state.zoom)));
    result.pageHeight = std::max(1, static_cast<int>(std::lround(spec.heightInches * 96.0 * state.zoom)));
    result.totalHeight = result.top + static_cast<int>(state.document.pages.size()) * (result.pageHeight + result.gap);
    return result;
}

void UpdatePaperButtons(PreviewState& state) {
    SetWindowTextW(state.letterButton,
                   state.paper == ScreenplayPaper::HollywoodLetter ? L"● Hollywood / US Letter" : L"Hollywood / US Letter");
    SetWindowTextW(state.a4Button, state.paper == ScreenplayPaper::A4 ? L"● A4" : L"A4");
}

void UpdateScrollBar(PreviewState& state) {
    RECT client{};
    GetClientRect(state.window, &client);
    const PreviewGeometry geometry = Geometry(state);
    const int maximum = std::max(0, geometry.totalHeight - static_cast<int>(client.bottom));
    state.scroll = std::clamp(state.scroll, 0, maximum);
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = geometry.totalHeight;
    info.nPage = static_cast<UINT>(std::max(1, static_cast<int>(client.bottom)));
    info.nPos = state.scroll;
    SetScrollInfo(state.window, SB_VERT, &info, TRUE);
}

void RebuildLayout(PreviewState& state, ScreenplayPaper paper) {
    state.paper = paper;
    ScreenplayLayoutService layout;
    state.document = layout.layout(state.project, paper, true);
    state.scroll = 0;
    UpdatePaperButtons(state);
    UpdateScrollBar(state);
    InvalidateRect(state.window, nullptr, FALSE);
}

int InchX(const RECT& page, const ScreenplayPageSpec& spec, double inches) {
    return page.left + static_cast<int>(std::lround((page.right - page.left) * inches / spec.widthInches));
}

int InchY(const RECT& page, const ScreenplayPageSpec& spec, double inches) {
    return page.top + static_cast<int>(std::lround((page.bottom - page.top) * inches / spec.heightInches));
}

HFONT PageFont(const RECT& page, const ScreenplayPageSpec& spec, double points, int weight,
               bool italic = false) {
    const double pixelsPerInch = static_cast<double>(page.bottom - page.top) / spec.heightInches;
    const int height = -std::max(7, static_cast<int>(std::lround(points * pixelsPerInch / 72.0)));
    return CreateFontW(height, 0, 0, 0, weight, italic, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       FIXED_PITCH | FF_MODERN, L"Courier New");
}

void DrawCentered(HDC dc, const std::wstring& text, RECT area, HFONT font, UINT extraFlags = 0) {
    if (text.empty()) return;
    const HGDIOBJ previous = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &area,
              DT_CENTER | DT_NOPREFIX | DT_WORDBREAK | extraFlags);
    SelectObject(dc, previous);
}

HDC CreatePrinterContext(const std::wstring& printer, ScreenplayPaper paper,
                         std::vector<BYTE>& storage) {
    HANDLE handle{};
    if (!OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &handle, nullptr)) return nullptr;
    const LONG bytes = DocumentPropertiesW(nullptr, handle, const_cast<LPWSTR>(printer.c_str()),
                                           nullptr, nullptr, 0);
    if (bytes <= 0) {
        ClosePrinter(handle);
        return CreateDCW(L"WINSPOOL", printer.c_str(), nullptr, nullptr);
    }
    storage.resize(static_cast<std::size_t>(bytes));
    auto* mode = reinterpret_cast<DEVMODEW*>(storage.data());
    if (DocumentPropertiesW(nullptr, handle, const_cast<LPWSTR>(printer.c_str()),
                            mode, nullptr, DM_OUT_BUFFER) != IDOK) {
        ClosePrinter(handle);
        return nullptr;
    }
    mode->dmFields |= DM_PAPERSIZE | DM_ORIENTATION;
    mode->dmPaperSize = paper == ScreenplayPaper::A4 ? DMPAPER_A4 : DMPAPER_LETTER;
    mode->dmOrientation = DMORIENT_PORTRAIT;
    DocumentPropertiesW(nullptr, handle, const_cast<LPWSTR>(printer.c_str()),
                        mode, mode, DM_IN_BUFFER | DM_OUT_BUFFER);
    HDC dc = CreateDCW(L"WINSPOOL", printer.c_str(), nullptr, mode);
    ClosePrinter(handle);
    return dc;
}

LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PreviewState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<PreviewState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_CREATE:
            state->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH, L"Segoe UI");
            state->exportButton = ui::CreateChild(L"BUTTON", L"Сохранить PDF", BS_PUSHBUTTON | BS_FLAT,
                                                   ExportButton, window,
                                                   reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
            state->printButton = ui::CreateChild(L"BUTTON", L"Печать", BS_PUSHBUTTON | BS_FLAT,
                                                  PrintButton, window,
                                                  reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
            state->letterButton = ui::CreateChild(L"BUTTON", L"Hollywood / US Letter", BS_PUSHBUTTON | BS_FLAT,
                                                   LetterButton, window,
                                                   reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
            state->a4Button = ui::CreateChild(L"BUTTON", L"A4", BS_PUSHBUTTON | BS_FLAT,
                                              A4Button, window,
                                              reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
            for (HWND button : {state->exportButton, state->printButton, state->letterButton, state->a4Button})
                ui::SetFont(button, state->font);
            UpdatePaperButtons(*state);
            return 0;
        case WM_SIZE:
            SetWindowPos(state->exportButton, nullptr, 14, 9, 136, 34, SWP_NOZORDER);
            SetWindowPos(state->printButton, nullptr, 160, 9, 104, 34, SWP_NOZORDER);
            SetWindowPos(state->letterButton, nullptr, 274, 9, 210, 34, SWP_NOZORDER);
            SetWindowPos(state->a4Button, nullptr, 494, 9, 72, 34, SWP_NOZORDER);
            UpdateScrollBar(*state);
            return 0;
        case WM_MOUSEWHEEL: {
            const bool ctrl = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
            if (ctrl) {
                state->zoom = std::clamp(state->zoom +
                    (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 0.08f : -0.08f), 0.35f, 1.5f);
            } else {
                state->scroll -= GET_WHEEL_DELTA_WPARAM(wParam);
            }
            UpdateScrollBar(*state);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(window, SB_VERT, &info);
            int next = state->scroll;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: next -= 48; break;
                case SB_LINEDOWN: next += 48; break;
                case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
                case SB_THUMBTRACK: next = info.nTrackPos; break;
                default: break;
            }
            state->scroll = next;
            UpdateScrollBar(*state);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == LetterButton) {
                RebuildLayout(*state, ScreenplayPaper::HollywoodLetter);
                return 0;
            }
            if (LOWORD(wParam) == A4Button) {
                RebuildLayout(*state, ScreenplayPaper::A4);
                return 0;
            }
            if (LOWORD(wParam) == ExportButton) {
                const auto file = ui::OpenFileDialog(window, true,
                    L"PDF (*.pdf)\0*.pdf\0Все файлы\0*.*\0\0", L"pdf", state->project.title + L".pdf");
                if (!file.empty()) {
                    std::wstring error;
                    if (!state->service->exportPdf(window, state->project, file, state->paper, &error))
                        MessageBoxW(window, error.c_str(), L"Mezozoy", MB_ICONERROR);
                }
                return 0;
            }
            if (LOWORD(wParam) == PrintButton) {
                std::wstring error;
                if (!state->service->print(window, state->project, state->paper, &error) && !error.empty())
                    MessageBoxW(window, error.c_str(), L"Mezozoy", MB_ICONERROR);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(window, &ps);
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH background = CreateSolidBrush(RGB(38, 38, 42));
            FillRect(dc, &client, background);
            DeleteObject(background);

            const PreviewGeometry geometry = Geometry(*state);
            const int x = std::max(10, (static_cast<int>(client.right) - geometry.pageWidth) / 2);
            int y = geometry.top - state->scroll;
            for (const ScreenplayLayoutPage& layoutPage : state->document.pages) {
                if (y + geometry.pageHeight >= 44 && y <= client.bottom) {
                    RECT shadow{x + 7, y + 8, x + geometry.pageWidth + 7, y + geometry.pageHeight + 8};
                    HBRUSH shadowBrush = CreateSolidBrush(RGB(12, 12, 14));
                    FillRect(dc, &shadow, shadowBrush);
                    DeleteObject(shadowBrush);
                    RECT page{x, y, x + geometry.pageWidth, y + geometry.pageHeight};
                    HBRUSH paper = CreateSolidBrush(RGB(255, 255, 255));
                    FillRect(dc, &page, paper);
                    DeleteObject(paper);
                    state->service->drawPage(dc, page, layoutPage, state->document, state->project, true);
                }
                y += geometry.pageHeight + geometry.gap;
            }
            EndPaint(window, &ps);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (state->font) DeleteObject(state->font);
            EnableWindow(state->owner, TRUE);
            SetForegroundWindow(state->owner);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

void PrintService::drawPage(HDC dc, const RECT& page, const ScreenplayLayoutPage& layoutPage,
                            const ScreenplayLayoutDocument& document, const Project& project,
                            bool) const {
    const ScreenplayPageSpec& spec = document.spec;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(18, 18, 18));

    HFONT normal = PageFont(page, spec, 12.0, FW_NORMAL);
    HFONT bold = PageFont(page, spec, 12.0, FW_BOLD);
    HFONT italic = PageFont(page, spec, 12.0, FW_NORMAL, true);
    HFONT titleFont = PageFont(page, spec, 18.0, FW_BOLD);

    if (layoutPage.titlePage) {
        const int left = InchX(page, spec, spec.marginLeftInches);
        const int right = InchX(page, spec, spec.widthInches - spec.marginRightInches);
        RECT titleArea{left, InchY(page, spec, 3.15), right, InchY(page, spec, 4.15)};
        DrawCentered(dc, project.title.empty() ? L"БЕЗ НАЗВАНИЯ" : utf::ToUpper(project.title),
                     titleArea, titleFont);

        int y = InchY(page, spec, 4.2);
        const int row = std::max(14, InchY(page, spec, 0.32) - static_cast<int>(page.top));
        auto centeredRow = [&](const std::wstring& value, HFONT font = nullptr) {
            if (value.empty()) return;
            RECT area{left, y, right, y + row * 2};
            DrawCentered(dc, value, area, font ? font : normal, DT_SINGLELINE);
            y += row;
        };
        centeredRow(project.subtitle);
        if (!project.author.empty()) {
            y += row / 2;
            centeredRow(L"Сценарий");
            centeredRow(project.author, bold);
        }
        if (!project.genre.empty()) centeredRow(project.genre);
        if (!project.projectType.empty()) centeredRow(project.projectType);
        if (!project.draftLabel.empty()) centeredRow(project.draftLabel);
        if (!project.year.empty()) centeredRow(project.year);

        const int bottomTop = InchY(page, spec, spec.heightInches - 1.75);
        RECT contact{left, bottomTop, InchX(page, spec, spec.widthInches / 2.0),
                     InchY(page, spec, spec.heightInches - 0.75)};
        RECT copyright{InchX(page, spec, spec.widthInches / 2.0), bottomTop, right,
                       InchY(page, spec, spec.heightInches - 0.75)};
        const HGDIOBJ previous = SelectObject(dc, normal);
        if (!project.contact.empty())
            DrawTextW(dc, project.contact.c_str(), -1, &contact, DT_LEFT | DT_BOTTOM | DT_WORDBREAK | DT_NOPREFIX);
        if (!project.copyright.empty())
            DrawTextW(dc, project.copyright.c_str(), -1, &copyright, DT_RIGHT | DT_BOTTOM | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, previous);
    } else {
        const double pixelsPerInch = static_cast<double>(page.right - page.left) / spec.widthInches;
        for (const ScreenplayRenderLine& line : layoutPage.lines) {
            const ScreenplayElementMetrics metrics = ScreenplayLayoutService::elementMetrics(line.type, spec);
            double left = metrics.leftInches;
            if (metrics.rightAligned) left += std::max(0.0, metrics.widthInches - line.text.size() * .1);
            if (metrics.centered) left += std::max(0.0, (metrics.widthInches - line.text.size() * .1) / 2);
            const int x = InchX(page, spec, left);
            const int y = InchY(page, spec, spec.marginTopInches + line.slot * spec.lineHeightInches);
            HFONT font = metrics.bold ? bold : (metrics.italic ? italic : normal);
            const HGDIOBJ previous = SelectObject(dc, font);
            std::wstring text = line.text;
            if (line.type == ScriptLineType::SceneHeading || line.type == ScriptLineType::Character ||
                line.type == ScriptLineType::Transition)
                text = utf::ToUpper(text);
            std::replace(text.begin(), text.end(), L'\t', L' ');
            std::vector<int> advances(text.size());
            for (std::size_t i = 0; i < advances.size(); ++i)
                advances[i] = static_cast<int>(std::lround((i + 1) * pixelsPerInch / 10) - std::lround(i * pixelsPerInch / 10));
            ExtTextOutW(dc, x, y, ETO_CLIPPED, &page, text.c_str(), static_cast<UINT>(text.size()), advances.data());
            SelectObject(dc, previous);
        }

        if (layoutPage.scriptPageNumber > 1) {
            const std::wstring number = std::to_wstring(layoutPage.scriptPageNumber) + L".";
            RECT numberArea{InchX(page, spec, spec.widthInches - 2.0), InchY(page, spec, 0.45),
                            InchX(page, spec, spec.widthInches - spec.marginRightInches),
                            InchY(page, spec, 0.8)};
            const HGDIOBJ previous = SelectObject(dc, normal);
            DrawTextW(dc, number.c_str(), -1, &numberArea, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, previous);
        }
    }

    DeleteObject(titleFont);
    DeleteObject(italic);
    DeleteObject(bold);
    DeleteObject(normal);
}

void PrintService::showPreview(HWND owner, const Project& project, ScreenplayPaper paper) {
    static bool registered = false;
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PreviewProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = PreviewClass;
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto state = std::make_unique<PreviewState>();
    state->service = this;
    state->project = project;
    state->paper = paper;
    state->owner = owner;
    ScreenplayLayoutService layout;
    state->document = layout.layout(project, paper, true);
    EnableWindow(owner, FALSE);
    HWND window = CreateWindowExW(0, PreviewClass, L"Предпросмотр сценария · F12",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_VSCROLL,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1200, 900,
                                  owner, nullptr, instance, state.get());
    if (!window) {
        EnableWindow(owner, TRUE);
        return;
    }
    ui::CenterWindow(window, owner);
    UpdateScrollBar(*state);
    MSG message{};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

std::wstring PrintService::pdfPrinter() {
    DWORD needed = 0;
    DWORD count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                  nullptr, 0, &needed, &count);
    if (!needed) return L"Microsoft Print to PDF";
    std::vector<BYTE> buffer(needed);
    if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                       buffer.data(), needed, &needed, &count))
        return L"Microsoft Print to PDF";
    auto* printers = reinterpret_cast<PRINTER_INFO_2W*>(buffer.data());
    for (DWORD index = 0; index < count; ++index) {
        const std::wstring name = printers[index].pPrinterName ? printers[index].pPrinterName : L"";
        if (utf::ToUpper(name).find(L"PDF") != std::wstring::npos) return name;
    }
    return L"Microsoft Print to PDF";
}

bool PrintService::exportPdf(HWND, const Project& project, const std::filesystem::path& file,
                             ScreenplayPaper paper, std::wstring* error) {
    const std::wstring printer = pdfPrinter();
    std::vector<BYTE> modeStorage;
    HDC dc = CreatePrinterContext(printer, paper, modeStorage);
    if (!dc) {
        if (error) *error = L"Не найден системный PDF-принтер. Включите компонент Microsoft Print to PDF.";
        return false;
    }

    DOCINFOW info{};
    info.cbSize = sizeof(info);
    info.lpszDocName = project.title.c_str();
    info.lpszOutput = file.c_str();
    if (StartDocW(dc, &info) <= 0) {
        if (error) *error = L"Не удалось начать создание PDF.";
        DeleteDC(dc);
        return false;
    }

    ScreenplayLayoutService layout;
    const ScreenplayLayoutDocument document = layout.layout(project, paper, true);
    bool ok = true;
    for (const ScreenplayLayoutPage& layoutPage : document.pages) {
        if (StartPage(dc) <= 0) {
            ok = false;
            break;
        }
        const int physicalWidth = GetDeviceCaps(dc, PHYSICALWIDTH);
        const int physicalHeight = GetDeviceCaps(dc, PHYSICALHEIGHT);
        const int offsetX = GetDeviceCaps(dc, PHYSICALOFFSETX);
        const int offsetY = GetDeviceCaps(dc, PHYSICALOFFSETY);
        RECT page{-offsetX, -offsetY, physicalWidth - offsetX, physicalHeight - offsetY};
        drawPage(dc, page, layoutPage, document, project, false);
        if (EndPage(dc) <= 0) {
            ok = false;
            break;
        }
    }
    if (ok) ok = EndDoc(dc) > 0; else AbortDoc(dc);
    DeleteDC(dc);
    if (!ok && error) *error = L"Ошибка при записи страниц PDF.";
    return ok;
}

bool PrintService::print(HWND owner, const Project& project, ScreenplayPaper paper,
                         std::wstring* error) {
    ScreenplayLayoutService layout;
    const ScreenplayLayoutDocument document = layout.layout(project, paper, true);

    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS |
                   PD_USEDEVMODECOPIESANDCOLLATE;
    dialog.nMinPage = 1;
    dialog.nMaxPage = static_cast<WORD>(std::min<std::size_t>(document.pages.size(), 65535));
    dialog.nFromPage = 1;
    dialog.nToPage = dialog.nMaxPage;
    if (!PrintDlgW(&dialog)) {
        if (CommDlgExtendedError() != 0 && error) *error = L"Не удалось открыть системное окно печати.";
        return CommDlgExtendedError() == 0;
    }

    if (dialog.hDevMode) {
        auto* mode = static_cast<DEVMODEW*>(GlobalLock(dialog.hDevMode));
        if (mode) {
            mode->dmFields |= DM_PAPERSIZE | DM_ORIENTATION;
            mode->dmPaperSize = paper == ScreenplayPaper::A4 ? DMPAPER_A4 : DMPAPER_LETTER;
            mode->dmOrientation = DMORIENT_PORTRAIT;
            if (HDC reset = ResetDCW(dialog.hDC, mode)) dialog.hDC = reset;
            GlobalUnlock(dialog.hDevMode);
        }
    }

    DOCINFOW info{};
    info.cbSize = sizeof(info);
    info.lpszDocName = project.title.empty() ? L"Mezozoy — сценарий" : project.title.c_str();
    bool ok = StartDocW(dialog.hDC, &info) > 0;
    if (ok) {
        for (const ScreenplayLayoutPage& layoutPage : document.pages) {
            if (StartPage(dialog.hDC) <= 0) {
                ok = false;
                break;
            }
            const int physicalWidth = GetDeviceCaps(dialog.hDC, PHYSICALWIDTH);
            const int physicalHeight = GetDeviceCaps(dialog.hDC, PHYSICALHEIGHT);
            const int offsetX = GetDeviceCaps(dialog.hDC, PHYSICALOFFSETX);
            const int offsetY = GetDeviceCaps(dialog.hDC, PHYSICALOFFSETY);
            RECT page{-offsetX, -offsetY, physicalWidth - offsetX, physicalHeight - offsetY};
            drawPage(dialog.hDC, page, layoutPage, document, project, false);
            if (EndPage(dialog.hDC) <= 0) {
                ok = false;
                break;
            }
        }
        if (ok) EndDoc(dialog.hDC); else AbortDoc(dialog.hDC);
    }

    if (dialog.hDC) DeleteDC(dialog.hDC);
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    if (!ok && error) *error = L"Windows не смогла напечатать страницы сценария.";
    return ok;
}

}  // namespace mezozoy
