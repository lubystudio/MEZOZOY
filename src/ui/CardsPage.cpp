#include "CardsPage.h"

#include "Win32Util.h"

#include <algorithm>

namespace mezozoy::ui {

CardsPage::CardsPage(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme)
    : NativePage(instance, parent, theme), settings_(settings) {
    title_ = CreateChild(L"STATIC", L"ДОСКА СЦЕН", SS_LEFT, 0, hwnd_, instance_);
    free_ = CreateChild(L"BUTTON", L"Свободно", BS_PUSHBUTTON | BS_FLAT, IdFree, hwnd_, instance_);
    columns_ = CreateChild(L"BUTTON", L"Колонки", BS_PUSHBUTTON | BS_FLAT, IdColumns, hwnd_, instance_);
    timelineRows_ = CreateChild(L"BUTTON", L"По порядку", BS_PUSHBUTTON | BS_FLAT, IdTimelineRows, hwnd_, instance_);
    fit_ = CreateChild(L"BUTTON", L"Вписать всё", BS_PUSHBUTTON | BS_FLAT, IdFit, hwnd_, instance_);
    for (HWND control : {title_, free_, columns_, timelineRows_, fit_}) remember(control, control == title_);
    board_ = new BoardCanvas(instance, hwnd_, document, settings, theme);
    board_->setDiagnostics(settings.get().diagnostics);
}

CardsPage::~CardsPage() { delete board_; }

void CardsPage::onLayout(int width, int height) {
    const int top = std::max(48, static_cast<int>(52 * uiScale_));
    const int buttonWidth = std::max(92, static_cast<int>(112 * uiScale_));
    SetWindowPos(title_, nullptr, 18, 12, std::max(140, width - buttonWidth * 4 - 64), 30, SWP_NOZORDER);
    int x = std::max(170, width - buttonWidth * 4 - 24);
    for (HWND button : {free_, columns_, timelineRows_, fit_}) {
        SetWindowPos(button, nullptr, x, 8, buttonWidth - 6, 34, SWP_NOZORDER);
        x += buttonWidth;
    }
    board_->layout(0, top, width, std::max(0, height - top));
}

bool CardsPage::handleCommand(int id, int code, HWND) {
    if (code != BN_CLICKED) return false;
    if (id == IdFree) { board_->useFreeLayout(); return true; }
    if (id == IdColumns) { board_->arrangeColumns(); return true; }
    if (id == IdTimelineRows) { board_->arrangeTimelineRows(); return true; }
    if (id == IdFit) { board_->fitAll(); return true; }
    return false;
}

void CardsPage::setOpenSceneAction(std::function<void()> action) { board_->setOpenSceneAction(std::move(action)); }

void CardsPage::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    board_->applyTheme(theme);
    board_->setDiagnostics(settings_.get().diagnostics);
}

}  // namespace mezozoy::ui
