#pragma once

#include "Page.h"
#include "../services/ProjectDocument.h"
#include "../services/SettingsService.h"
#include "../services/StatisticsService.h"

#include <gdiplus.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace mezozoy::ui {

class HomePage final : public NativePage {
public:
    HomePage(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme);
    ~HomePage() override;
    void onLayout(int width, int height) override;
    bool handleCommand(int id, int code, HWND source) override;
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void applyAppearance(const Theme& theme, float uiScale) override;
    void setCreateAction(std::function<void()> action) { create_ = std::move(action); }
    void setOpenAction(std::function<void()> action) { open_ = std::move(action); }
    void setOpenPathAction(std::function<void(const std::filesystem::path&)> action) { openPath_ = std::move(action); }
    void setSaveAction(std::function<bool()> action) { save_ = std::move(action); }
    void setOpenScriptAction(std::function<void()> action) { openScriptAction_ = std::move(action); }
    void setOpenCardsAction(std::function<void()> action) { openCardsAction_ = std::move(action); }
    void setOpenDevelopmentAction(std::function<void()> action) { openDevelopmentAction_ = std::move(action); }
    void setNewSceneAction(std::function<void()> action) { newSceneAction_ = std::move(action); }
    void setOpenSceneAction(std::function<void(std::size_t)> action) { openSceneAction_ = std::move(action); }
    void setProjectActive(bool active);
    void refreshRecent();

private:
    struct RecentProject {
        std::filesystem::path path;
        std::wstring title;
        std::wstring subtitle;
        std::wstring modified;
        std::unique_ptr<Gdiplus::Bitmap> poster;
        RECT card{};
        RECT posterAction{};
        bool currentDocument = false;
    };

    enum : int {
        IdCreate = 3101, IdOpen, IdInstruction, IdQuickCreate, IdQuickOpen,
        IdOpenScript, IdOpenCards, IdNewScene, IdOpenDevelopment
    };
    ProjectDocument& document_;
    SettingsService& settings_;
    HWND quote_{};
    HWND quoteAuthor_{};
    HWND createButton_{};
    HWND openButton_{};
    HWND instructionButton_{};
    HWND quickCreate_{};
    HWND quickOpen_{};
    HWND openScript_{};
    HWND openCards_{};
    HWND newScene_{};
    HWND openDevelopment_{};
    std::vector<RecentProject> recentProjects_;
    std::function<void()> create_;
    std::function<void()> open_;
    std::function<void(const std::filesystem::path&)> openPath_;
    std::function<bool()> save_;
    std::function<void()> openScriptAction_;
    std::function<void()> openCardsAction_;
    std::function<void()> openDevelopmentAction_;
    std::function<void()> newSceneAction_;
    std::function<void(std::size_t)> openSceneAction_;
    StatisticsService statistics_;
    ULONG_PTR gdiplusToken_ = 0;
    int width_ = 0;
    int height_ = 0;
    int leftWidth_ = 410;
    int scrollOffset_ = 0;
    int contentHeight_ = 0;
    int hoveredProject_ = -1;
    bool posterActionHovered_ = false;
    bool projectActive_ = false;
    std::vector<RECT> dashboardSceneRects_;
    std::vector<std::size_t> dashboardSceneIndices_;
    int hoveredDashboardScene_ = -1;
    std::unique_ptr<Gdiplus::Bitmap> currentPoster_;
    HDC backBufferDc_ = nullptr;
    HBITMAP backBufferBitmap_ = nullptr;
    HGDIOBJ backBufferPrevious_ = nullptr;
    int backBufferWidth_ = 0;
    int backBufferHeight_ = 0;

    void paint(HDC dc);
    void paintDashboard(HDC dc);
    void paintBuffered(HDC dc);
    bool ensureBackBuffer(HDC referenceDc, int width, int height);
    void releaseBackBuffer();
    void layoutCards();
    void layoutDashboard();
    void invalidateProject(int index);
    int hitProject(POINT point) const;
    bool hitPosterAction(int index, POINT point) const;
    void openProjectAt(int index);
    void posterMenu(int index, POINT screenPoint);
    void choosePoster(int index);
    void removePoster(int index);
    bool updatePoster(int index, const std::filesystem::path* imagePath);
    std::unique_ptr<Gdiplus::Bitmap> loadPoster(const Project& project) const;
    HBRUSH controlColor(HDC dc, HWND control, UINT message) override;
    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;
};

}  // namespace mezozoy::ui
