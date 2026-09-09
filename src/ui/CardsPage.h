#pragma once

#include "BoardCanvas.h"
#include "Page.h"
#include "../services/SettingsService.h"

#include <functional>

namespace mezozoy::ui {

class CardsPage final : public NativePage {
public:
    CardsPage(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme);
    ~CardsPage() override;
    void onLayout(int width, int height) override;
    void commit() override {}
    void applyAppearance(const Theme& theme, float uiScale) override;
    void setHostInteractionPaused(bool paused) override { board_->setRenderingSuspended(paused); }
    bool handleCommand(int id, int code, HWND source) override;
    void setOpenSceneAction(std::function<void()> action);
    void setStatusCallback(std::function<void(const std::wstring&)> callback) { board_->setStatusCallback(std::move(callback)); }
    bool copySelection(ProjectClipboard& clipboard) const { return board_->copySelection(clipboard); }
    bool pasteSelection(const ProjectClipboard& clipboard) { return board_->pasteSelection(clipboard); }
    void refresh() { board_->refresh(); }

private:
    enum : int { IdFree = 4701, IdColumns, IdTimelineRows, IdFit };
    SettingsService& settings_;
    BoardCanvas* board_{};
    HWND title_{};
    HWND free_{};
    HWND columns_{};
    HWND timelineRows_{};
    HWND fit_{};
};

}  // namespace mezozoy::ui
