#include "PagedScriptView.h"
#include "UiStyle.h"
#include "../core/Utf.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace mezozoy::ui {
namespace {
UINT Dpi(HWND window) {
    using Query = UINT(WINAPI*)(HWND);
    static const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return query ? std::max<UINT>(96, query(window)) : 96;
}
void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
std::wstring Normalize(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\r') {
            if (i + 1 < value.size() && value[i + 1] == L'\n') ++i;
            result += L'\n';
        } else result += value[i];
    }
    return result;
}
}

PagedScriptView::~PagedScriptView() {
    for (HWND bar : {verticalBar_, horizontalBar_}) if (IsWindow(bar)) DestroyWindow(bar);
}

void PagedScriptView::attach(HWND editor) {
    editor_ = editor;
    // RichEdit's text layout has a different height from paginated paper. Its
    // nonclient bars must not compete with the view's independent scroll controls.
    SendMessageW(editor_, EM_SHOWSCROLLBAR, SB_BOTH, FALSE);
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(editor_, GWLP_HINSTANCE));
    verticalBar_ = CreateChild(L"SCROLLBAR", L"Прокрутка страниц", SBS_VERT | WS_TABSTOP | WS_CLIPSIBLINGS, 1190, GetParent(editor_), instance);
    horizontalBar_ = CreateChild(L"SCROLLBAR", L"Прокрутка по горизонтали", SBS_HORZ | WS_TABSTOP | WS_CLIPSIBLINGS, 1191, GetParent(editor_), instance);
    for (HWND bar : {verticalBar_, horizontalBar_})
        SetWindowSubclass(bar, ScrollbarProc, 9790, reinterpret_cast<DWORD_PTR>(this));
    SetTimer(editor_, CaretTimer, std::max<UINT>(200, GetCaretBlinkTime()), nullptr);
}

RECT PagedScriptView::viewport() const {
    RECT rect{};
    GetClientRect(editor_, &rect);
    const int thickness = MulDiv(16, Dpi(editor_), 96);
    rect.right = std::max<LONG>(0, rect.right - thickness);
    rect.bottom = std::max<LONG>(0, rect.bottom - thickness);
    return rect;
}

LRESULT CALLBACK PagedScriptView::ScrollbarProc(HWND bar, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<PagedScriptView*>(data);
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT || msg == WM_PRINTCLIENT || msg == WM_PRINT) {
        PAINTSTRUCT ps{};
        HDC target = msg == WM_PAINT ? BeginPaint(bar, &ps) : reinterpret_cast<HDC>(wp);
        RECT r{}; GetClientRect(bar, &r);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, std::max<LONG>(1,r.right), std::max<LONG>(1,r.bottom));
        const auto oldBitmap = SelectObject(dc, bitmap);
        // Let the native control initialize its hit-test geometry offscreen.
        const int nativeDc = SaveDC(dc);
        DefSubclassProc(bar, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        RestoreDC(dc, nativeDc);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
        style::Fill(dc, r, self->theme_.background);
        SCROLLBARINFO info{}; info.cbSize = sizeof(info);
        if (GetScrollBarInfo(bar, OBJID_CLIENT, &info)) {
            const bool vertical = bar == self->verticalBar_;
            const int length = vertical ? r.bottom : r.right;
            RECT thumb = r;
            const int inset = std::max(3, MulDiv(5, Dpi(bar), 96));
            if (vertical) { thumb.left += inset; thumb.right -= inset; thumb.top = info.xyThumbTop; thumb.bottom = info.xyThumbBottom; }
            else { thumb.top += inset; thumb.bottom -= inset; thumb.left = info.xyThumbTop; thumb.right = info.xyThumbBottom; }
            const auto color = GetFocus() == bar ? self->theme_.accent : Theme::Blend(self->theme_.border, self->theme_.textMuted, 35);
            if (thumb.right > thumb.left && thumb.bottom > thumb.top) style::Surface(dc, thumb, color, color, 3);
            HPEN pen = CreatePen(PS_SOLID, 1, self->theme_.textMuted);
            const auto oldPen = SelectObject(dc, pen);
            const int middle = (vertical ? r.right : r.bottom) / 2;
            const int first = info.dxyLineButton / 2, last = length - first;
            if (vertical) {
                MoveToEx(dc,middle-3,first+1,nullptr); LineTo(dc,middle,first-2); LineTo(dc,middle+3,first+1);
                MoveToEx(dc,middle-3,last-1,nullptr); LineTo(dc,middle,last+2); LineTo(dc,middle+3,last-1);
            } else {
                MoveToEx(dc,first+1,middle-3,nullptr); LineTo(dc,first-2,middle); LineTo(dc,first+1,middle+3);
                MoveToEx(dc,last-1,middle-3,nullptr); LineTo(dc,last+2,middle); LineTo(dc,last-1,middle+3);
            }
            SelectObject(dc,oldPen); DeleteObject(pen);
        }
        RestoreDC(dc, saved);
        BitBlt(target, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
        if (msg == WM_PAINT) EndPaint(bar, &ps);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) return SendMessageW(self->editor_, msg, wp, lp);
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(bar, ScrollbarProc, id);
    const auto result = DefSubclassProc(bar, msg, wp, lp);
    if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
        InvalidateRect(bar, nullptr, FALSE);
    return result;
}

void PagedScriptView::setContent(const std::wstring& text, const std::wstring& formats, ScreenplayPaper paper) {
    const std::wstring normalized = Normalize(text);
    if (normalized == text_ && formats == formats_ && document_.paper == paper && !document_.pages.empty()) return;
    text_ = normalized;
    formats_ = formats;
    document_ = ScreenplayLayoutService{}.layoutText(text_, formats_, paper);
    rows_.clear();
    for (std::size_t p = 0; p < document_.pages.size(); ++p)
        for (const auto& line : document_.pages[p].lines) rows_.push_back({p, &line});
    desiredX_.reset();
    resize();
}

void PagedScriptView::setTheme(const Theme& theme) {
    theme_ = theme;
    if (editor_) InvalidateRect(editor_, nullptr, FALSE);
    for (HWND bar : {verticalBar_, horizontalBar_}) if (bar) InvalidateRect(bar, nullptr, FALSE);
}

int PagedScriptView::zoom() const {
    return static_cast<int>(std::lround(pixelsPerInch_ / Dpi(editor_) * 100));
}

void PagedScriptView::setZoom(int percent) {
    zoomPercent_ = percent == 0 ? 0 : std::clamp(percent, 50, 200);
    resize();
    revealCaret();
}

void PagedScriptView::resize() {
    if (!editor_) return;
    const RECT client = viewport();
    const int thickness = MulDiv(16, Dpi(editor_), 96);
    POINT origin{}; MapWindowPoints(editor_, GetParent(editor_), &origin, 1);
    SetWindowPos(verticalBar_, HWND_TOP, origin.x + client.right, origin.y, thickness, client.bottom, SWP_NOACTIVATE);
    SetWindowPos(horizontalBar_, HWND_TOP, origin.x, origin.y + client.bottom, client.right, thickness, SWP_NOACTIVATE);
    const double dpi = Dpi(editor_);
    const auto& spec = document_.spec;
    const double scale = zoomPercent_ == 0
        ? std::min(1.25, std::max(0.35, (client.right - 40.0) / (spec.widthInches * dpi)))
        : zoomPercent_ / 100.0;
    pixelsPerInch_ = dpi * scale;
    pageWidth_ = static_cast<int>(std::lround(spec.widthInches * pixelsPerInch_));
    pageHeight_ = static_cast<int>(std::lround(spec.heightInches * pixelsPerInch_));
    gap_ = std::max(20, static_cast<int>(std::lround(dpi * 0.25)));
    updateScrollbars();
    InvalidateRect(editor_, nullptr, FALSE);
}

void PagedScriptView::updateScrollbars() {
    const RECT client = viewport();
    const int height = gap_ + static_cast<int>(document_.pages.size()) * (pageHeight_ + gap_);
    scrollY_ = std::clamp(scrollY_, 0, std::max(0, height - static_cast<int>(client.bottom)));
    scrollX_ = std::clamp(scrollX_, 0, std::max(0, pageWidth_ + 40 - static_cast<int>(client.right)));
    SCROLLINFO vertical{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, height - 1),
                        static_cast<UINT>(client.bottom), scrollY_, 0};
    SCROLLINFO horizontal{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS, 0, pageWidth_ + 39,
                          static_cast<UINT>(client.right), scrollX_, 0};
    const auto update = [](HWND bar, SCROLLINFO& wanted) {
        SCROLLINFO current{}; current.cbSize = sizeof(current); current.fMask = wanted.fMask;
        GetScrollInfo(bar, SB_CTL, &current);
        if (current.nMin != wanted.nMin || current.nMax != wanted.nMax || current.nPage != wanted.nPage || current.nPos != wanted.nPos) {
            SetScrollInfo(bar, SB_CTL, &wanted, FALSE);
            InvalidateRect(bar, nullptr, FALSE);
        }
    };
    update(verticalBar_, vertical);
    update(horizontalBar_, horizontal);
    const bool horizontalVisible = pageWidth_ + 40 > client.right;
    if (static_cast<bool>(GetWindowLongPtrW(horizontalBar_, GWL_STYLE) & WS_VISIBLE) != horizontalVisible)
        ShowWindow(horizontalBar_, horizontalVisible ? SW_SHOWNA : SW_HIDE);
}

RECT PagedScriptView::pageRect(std::size_t page) const {
    const RECT client = viewport();
    const int x = std::max(20, (static_cast<int>(client.right) - pageWidth_) / 2) - scrollX_;
    const int y = gap_ + static_cast<int>(page) * (pageHeight_ + gap_) - scrollY_;
    return {x, y, x + pageWidth_, y + pageHeight_};
}

RECT PagedScriptView::rowRect(const Row& row) const {
    const auto paper = pageRect(row.page);
    const auto& spec = document_.spec;
    const auto metric = ScreenplayLayoutService::elementMetrics(row.line->type, spec);
    double x = metric.leftInches;
    const double textWidth = row.line->text.size() * 0.1;
    if (metric.rightAligned) x += std::max(0.0, metric.widthInches - textWidth);
    if (metric.centered) x += std::max(0.0, (metric.widthInches - textWidth) / 2);
    const LONG left = paper.left + static_cast<LONG>(std::lround(x * pixelsPerInch_));
    const LONG top = paper.top + static_cast<LONG>(std::lround(
        (spec.marginTopInches + row.line->slot * spec.lineHeightInches) * pixelsPerInch_));
    return {left, top, left + static_cast<LONG>(std::lround(textWidth * pixelsPerInch_)),
            top + static_cast<LONG>(std::lround(spec.lineHeightInches * pixelsPerInch_))};
}

std::size_t PagedScriptView::rowAt(LONG position) const {
    if (rows_.empty()) return 0;
    const auto found = std::upper_bound(rows_.begin(), rows_.end(), static_cast<std::size_t>(std::max<LONG>(0, position)),
        [](std::size_t cp, const Row& row) { return cp < row.line->sourceStart; });
    return found == rows_.begin() ? 0 : static_cast<std::size_t>(found - rows_.begin() - 1);
}

POINT PagedScriptView::characterPoint(LONG position) const {
    if (rows_.empty()) return {0, 0};
    const Row& row = rows_[rowAt(position)];
    const RECT rect = rowRect(row);
    const auto column = std::clamp<LONG>(position - static_cast<LONG>(row.line->sourceStart), 0,
                                       static_cast<LONG>(row.line->text.size()));
    return {rect.left + static_cast<LONG>(std::lround(column * pixelsPerInch_ / 10.0)), rect.top};
}

LONG PagedScriptView::hitTest(POINT point) const {
    if (rows_.empty()) return 0;
    std::size_t best = 0;
    LONG bestDistance = MAXLONG;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const RECT rect = rowRect(rows_[i]);
        const LONG distance = point.y < rect.top ? rect.top - point.y
            : point.y >= rect.bottom ? point.y - rect.bottom + 1 : 0;
        if (distance < bestDistance) { best = i; bestDistance = distance; }
        if (distance == 0) break;
    }
    const Row& row = rows_[best];
    const RECT rect = rowRect(row);
    const auto column = std::clamp<long>(std::lround((point.x - rect.left) * 10.0 / pixelsPerInch_),
                                         0, static_cast<long>(row.line->text.size()));
    return static_cast<LONG>(row.line->sourceStart) + column;
}

void PagedScriptView::selectionChanged(bool reveal) {
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (selection.cpMin != lastSelection_.cpMin || selection.cpMax != lastSelection_.cpMax) {
        anchor_ = selection.cpMin;
        caret_ = selection.cpMax;
        lastSelection_ = selection;
    }
    caretVisible_ = true;
    if (reveal) revealCaret();
    InvalidateRect(editor_, nullptr, FALSE);
}

void PagedScriptView::select(LONG position, bool extend) {
    position = std::clamp<LONG>(position, 0, static_cast<LONG>(text_.size()));
    const LONG anchor = extend ? anchor_ : position;
    CHARRANGE selection{std::min(anchor, position), std::max(anchor, position)};
    SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    anchor_ = anchor;
    caret_ = position;
    lastSelection_ = selection;
    caretVisible_ = true;
    revealCaret();
    InvalidateRect(editor_, nullptr, FALSE);
}

void PagedScriptView::scroll(int x, int y) {
    scrollX_ = x;
    scrollY_ = y;
    updateScrollbars();
    InvalidateRect(editor_, nullptr, FALSE);
}

void PagedScriptView::revealCaret() {
    const POINT point = characterPoint(caret_);
    RECT client{};
    GetClientRect(editor_, &client);
    const int padding = std::max(30, static_cast<int>(pixelsPerInch_ / 3));
    int y = scrollY_, x = scrollX_;
    if (point.y < padding) y += point.y - padding;
    else if (point.y > client.bottom - padding) y += point.y - (client.bottom - padding);
    if (point.x < 12) x += point.x - 12;
    else if (point.x > client.right - 16) x += point.x - (client.right - 16);
    if (caret_ == 0) { x = 0; y = 0; }
    if (x != scrollX_ || y != scrollY_) scroll(x, y);
}

bool PagedScriptView::navigate(WPARAM key, bool shift, bool ctrl) {
    if (rows_.empty()) return false;
    selectionChanged();
    const std::size_t index = rowAt(caret_);
    const Row& row = rows_[index];
    if (key == VK_HOME || key == VK_END) {
        LONG cp = key == VK_HOME ? static_cast<LONG>(row.line->sourceStart) : static_cast<LONG>(row.line->sourceEnd);
        if (ctrl) cp = key == VK_HOME ? 0 : static_cast<LONG>(text_.size());
        desiredX_.reset();
        select(cp, shift);
        return true;
    }
    if ((key == VK_UP || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT) && !ctrl) {
        const POINT point = characterPoint(caret_);
        if (!desiredX_) desiredX_ = point.x;
        if (key == VK_PRIOR || key == VK_NEXT) {
            RECT client{};
            GetClientRect(editor_, &client);
            select(hitTest({*desiredX_, point.y + (key == VK_PRIOR ? -1 : 1) * std::max<LONG>(32, client.bottom - 40)}), shift);
        } else {
            const int next = std::clamp(static_cast<int>(index) + (key == VK_UP ? -1 : 1), 0, static_cast<int>(rows_.size()) - 1);
            const RECT rect = rowRect(rows_[next]);
            select(hitTest({*desiredX_, rect.top + 1}), shift);
        }
        return true;
    }
    desiredX_.reset();
    return false;
}

void PagedScriptView::paint(HDC target) {
    RECT client{};
    GetClientRect(editor_, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    Fill(dc, client, theme_.background);
    const int fontHeight = std::max(8, static_cast<int>(std::lround(document_.spec.fontPoints / 72 * pixelsPerInch_)));
    HFONT fonts[4]{};
    for (int i = 0; i < 4; ++i) fonts[i] = CreateFontW(-fontHeight, 0, 0, 0, i & 1 ? FW_BOLD : FW_NORMAL,
        i & 2 ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
    const HGDIOBJ oldFont = SelectObject(dc, fonts[0]);
    SetBkMode(dc, TRANSPARENT);
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    for (std::size_t p = 0; p < document_.pages.size(); ++p) {
        const RECT paper = pageRect(p);
        if (paper.bottom < 0 || paper.top > client.bottom) continue;
        RECT shadow = paper;
        OffsetRect(&shadow, 3, 4);
        Fill(dc, shadow, Theme::Blend(theme_.background, RGB(0,0,0), theme_.light ? 10 : 30));
        Fill(dc, paper, theme_.light ? theme_.page : Theme::Blend(theme_.page, theme_.text, 4));
        HBRUSH border = CreateSolidBrush(theme_.border);
        FrameRect(dc, &paper, border);
        DeleteObject(border);
        SetTextColor(dc, theme_.textMuted);
        SelectObject(dc, fonts[0]);
        const std::wstring number = std::to_wstring(document_.pages[p].scriptPageNumber) + L".";
        RECT numberRect{paper.left, paper.top + static_cast<LONG>(pixelsPerInch_ * .45),
            paper.right - static_cast<LONG>(pixelsPerInch_ * document_.spec.marginRightInches),
            paper.top + static_cast<LONG>(pixelsPerInch_ * .8)};
        if (document_.pages[p].scriptPageNumber > 1)
            DrawTextW(dc, number.c_str(), -1, &numberRect, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        for (const auto& line : document_.pages[p].lines) {
            const Row row{p, &line};
            const RECT rect = rowRect(row);
            if (rect.bottom < 0 || rect.top > client.bottom) continue;
            const auto metrics = ScreenplayLayoutService::elementMetrics(line.type, document_.spec);
            SelectObject(dc, fonts[(metrics.bold ? 1 : 0) | (metrics.italic ? 2 : 0)]);
            std::wstring shown = line.text;
            std::replace(shown.begin(), shown.end(), L'\t', L' ');
            if (line.type == ScriptLineType::SceneHeading || line.type == ScriptLineType::Character ||
                line.type == ScriptLineType::Transition) shown = utf::ToUpper(shown);
            const LONG from = std::max<LONG>(selection.cpMin, static_cast<LONG>(line.sourceStart));
            const LONG to = std::min<LONG>(selection.cpMax, static_cast<LONG>(line.sourceEnd));
            if (selection.cpMin != selection.cpMax && to >= from && selection.cpMax > static_cast<LONG>(line.sourceStart)) {
                RECT highlight = rect;
                highlight.left += static_cast<LONG>(std::lround((from - line.sourceStart) * pixelsPerInch_ / 10));
                highlight.right = rect.left + static_cast<LONG>(std::lround((to - line.sourceStart) * pixelsPerInch_ / 10));
                if (selection.cpMax > static_cast<LONG>(line.sourceEnd)) highlight.right += static_cast<LONG>(pixelsPerInch_ / 10);
                Fill(dc, highlight, theme_.selection);
            }
            SetTextColor(dc, theme_.text);
            std::vector<int> advances(shown.size());
            for (std::size_t i = 0; i < advances.size(); ++i)
                advances[i] = static_cast<int>(std::lround((i + 1) * pixelsPerInch_ / 10) - std::lround(i * pixelsPerInch_ / 10));
            ExtTextOutW(dc, rect.left, rect.top, ETO_CLIPPED, &paper, shown.c_str(), static_cast<UINT>(shown.size()), advances.data());
        }
    }
    if (GetFocus() == editor_ && caretVisible_ && selection.cpMin == selection.cpMax) {
        const POINT point = characterPoint(selection.cpMax);
        RECT caret{point.x, point.y, point.x + 2, point.y + fontHeight};
        Fill(dc, caret, theme_.accent);
    }
    SelectObject(dc, oldFont);
    for (HFONT font : fonts) DeleteObject(font);
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

std::optional<LRESULT> PagedScriptView::message(UINT msg, WPARAM wParam, LPARAM lParam) {
    // The custom painter must honor the same transaction as its RichEdit backing.
    if (msg == WM_SETREDRAW) { redrawEnabled_ = wParam != FALSE; return std::nullopt; }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        HideCaret(editor_);
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(editor_, &ps);
        if (redrawEnabled_) paint(dc);
        EndPaint(editor_, &ps);
        return 0;
    }
    if (msg == WM_PRINTCLIENT || msg == WM_PRINT) {
        HideCaret(editor_); paint(reinterpret_cast<HDC>(wParam));
        return 0;
    }
    if (msg == EM_POSFROMCHAR && wParam) {
        const POINT point = characterPoint(static_cast<LONG>(lParam));
        *reinterpret_cast<POINTL*>(wParam) = {point.x, point.y};
        return 0;
    }
    if (msg == EM_SCROLLCARET) { selectionChanged(true); return 1; }
    if (msg == WM_TIMER && wParam == CaretTimer) {
        if (GetFocus() == editor_) { caretVisible_ = !caretVisible_; InvalidateRect(editor_, nullptr, FALSE); }
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) setZoom(zoom() + delta / WHEEL_DELTA * 10);
        else scroll(scrollX_, scrollY_ - static_cast<int>(delta / 120.0 * pixelsPerInch_ * .5));
        return 0;
    }
    if (msg == WM_VSCROLL || msg == WM_HSCROLL) {
        const bool vertical = msg == WM_VSCROLL;
        SCROLLINFO info{};
        info.cbSize = sizeof(info); info.fMask = SIF_ALL;
        GetScrollInfo(vertical ? verticalBar_ : horizontalBar_, SB_CTL, &info);
        int position = vertical ? scrollY_ : scrollX_;
        switch (LOWORD(wParam)) {
            case SB_LINEUP: position -= 32; break;
            case SB_LINEDOWN: position += 32; break;
            case SB_PAGEUP: position -= info.nPage; break;
            case SB_PAGEDOWN: position += info.nPage; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
            case SB_TOP: position = 0; break;
            case SB_BOTTOM: position = info.nMax; break;
        }
        scroll(vertical ? scrollX_ : position, vertical ? position : scrollY_);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        SetFocus(editor_);
        const LONG hit = hitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (msg == WM_LBUTTONDBLCLK) {
            LONG begin = hit, end = hit;
            while (begin > 0 && !std::iswspace(text_[begin - 1])) --begin;
            while (end < static_cast<LONG>(text_.size()) && !std::iswspace(text_[end])) ++end;
            select(begin, false);
            select(end, true);
        } else select(hit, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        dragging_ = true;
        SetCapture(editor_);
        return 0;
    }
    if (msg == WM_MOUSEMOVE && dragging_) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{};
        GetClientRect(editor_, &client);
        if (point.y < 0) scroll(scrollX_, scrollY_ - 24);
        if (point.y > client.bottom) scroll(scrollX_, scrollY_ + 24);
        select(hitTest(point), true);
        return 0;
    }
    if (msg == WM_LBUTTONUP && dragging_) { dragging_ = false; ReleaseCapture(); return 0; }
    if (msg == WM_CAPTURECHANGED) dragging_ = false;
    if (msg == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_IBEAM)); return TRUE; }
    return std::nullopt;
}

} // namespace mezozoy::ui
