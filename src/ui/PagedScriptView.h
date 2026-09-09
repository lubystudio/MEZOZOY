#pragma once

#include "Theme.h"
#include "../services/ScreenplayLayoutService.h"
#include <richedit.h>
#include <optional>

namespace mezozoy::ui {

// RichEdit owns text, clipboard and input. This view owns only paper geometry.
class PagedScriptView {
public:
    ~PagedScriptView();
    void attach(HWND editor);
    void setContent(const std::wstring& text, const std::wstring& formats, ScreenplayPaper paper);
    void setTheme(const Theme& theme);
    void selectionChanged(bool reveal = false);
    void resize();
    void resetScroll() { scroll(0, 0); }
    void setZoom(int percent); // zero fits the paper width
    int zoom() const;
    const ScreenplayLayoutDocument& document() const { return document_; }
    std::optional<LRESULT> message(UINT message, WPARAM wParam, LPARAM lParam);
    bool navigate(WPARAM key, bool shift, bool ctrl);

private:
    struct Row { std::size_t page; const ScreenplayRenderLine* line; };
    HWND editor_{};
    HWND verticalBar_{}, horizontalBar_{};
    Theme theme_;
    ScreenplayLayoutDocument document_;
    std::wstring text_, formats_;
    std::vector<Row> rows_;
    int zoomPercent_ = 0;
    int scrollY_ = 0, scrollX_ = 0;
    int pageWidth_ = 816, pageHeight_ = 1056, gap_ = 28;
    double pixelsPerInch_ = 96.0;
    bool dragging_ = false, caretVisible_ = true;
    bool redrawEnabled_ = true;
    LONG anchor_ = 0, caret_ = 0;
    CHARRANGE lastSelection_{0, 0};
    std::optional<int> desiredX_;
    static constexpr UINT_PTR CaretTimer = 9145;

    void paint(HDC dc);
    RECT viewport() const;
    static LRESULT CALLBACK ScrollbarProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void updateScrollbars();
    void scroll(int x, int y);
    RECT pageRect(std::size_t page) const;
    RECT rowRect(const Row& row) const;
    POINT characterPoint(LONG position) const;
    LONG hitTest(POINT point) const;
    std::size_t rowAt(LONG position) const;
    void select(LONG position, bool extend);
    void revealCaret();
};

} // namespace mezozoy::ui
