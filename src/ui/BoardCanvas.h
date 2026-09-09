#pragma once

#include "Theme.h"
#include "../services/ProjectClipboard.h"
#include "../services/ProjectDocument.h"
#include "../services/SettingsService.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace mezozoy::ui {

class BoardCanvas {
public:
    BoardCanvas(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme);
    ~BoardCanvas();

    HWND hwnd() const { return hwnd_; }
    void layout(int x, int y, int width, int height);
    void refresh();
    void applyTheme(const Theme& theme);
    void setRenderingSuspended(bool suspended);
    void setDiagnostics(bool value) { diagnostics_ = value; refresh(); }
    void setStatusCallback(std::function<void(const std::wstring&)> callback) { status_ = std::move(callback); }
    void setOpenSceneAction(std::function<void()> action) { openSceneAction_ = std::move(action); }
    void useFreeLayout();
    void arrangeColumns();
    void arrangeTimelineRows();
    void fitAll();
    bool copySelection(ProjectClipboard& clipboard) const;
    bool pasteSelection(const ProjectClipboard& clipboard);

private:
    enum class Mode { None, Panning, Zooming, PendingCard, PendingSection, DraggingCard, DraggingSection, ResizingSection, Selecting };
    struct Camera { float x = -80.0f; float y = -60.0f; float zoom = 1.0f; };
    struct Hit { int scene = -1; int section = -1; bool lock = false; int resizeMask = 0; };
    struct SceneTextCache {
        std::wstring title;
        std::wstring body;
        IDWriteTextLayout* titleLayout{};
        IDWriteTextLayout* bodyLayout{};
    };

    HINSTANCE instance_{};
    HWND hwnd_{};
    ProjectDocument& document_;
    SettingsService& settings_;
    Theme theme_;
    ID2D1Factory* factory_{};
    ID2D1PathGeometry* lockPaths_[2]{};
    int hoverSceneId_ = -1, hoverSectionId_ = -1;
    bool hoverLock_ = false;
    ID2D1HwndRenderTarget* target_{};
    IDWriteFactory* writeFactory_{};
    IDWriteTextFormat* titleFormat_{};
    IDWriteTextFormat* bodyFormat_{};
    IDWriteTextFormat* sectionFormat_{};
    ID2D1SolidColorBrush* gridMinorBrush_{};
    ID2D1SolidColorBrush* gridMajorBrush_{};
    ID2D1SolidColorBrush* sectionFillBrush_{};
    ID2D1SolidColorBrush* sectionHeaderBrush_{};
    ID2D1SolidColorBrush* shadowBrush_{};
    ID2D1SolidColorBrush* panelBrush_{};
    ID2D1SolidColorBrush* borderBrush_{};
    ID2D1SolidColorBrush* accentBrush_{};
    ID2D1SolidColorBrush* textBrush_{};
    ID2D1SolidColorBrush* mutedBrush_{};
    ID2D1SolidColorBrush* selectionForwardFillBrush_{};
    ID2D1SolidColorBrush* selectionForwardBorderBrush_{};
    ID2D1SolidColorBrush* selectionBackwardFillBrush_{};
    ID2D1SolidColorBrush* selectionBackwardBorderBrush_{};
    ID2D1SolidColorBrush* diagnosticsFillBrush_{};
    std::map<int, SceneTextCache> sceneTextCache_;
    Camera camera_;
    Mode mode_ = Mode::None;
    bool diagnostics_ = false;
    bool renderingSuspended_ = false;
    POINT mouseDown_{};
    POINT lastMouse_{};
    POINT selectionStart_{};
    POINT selectionEnd_{};
    bool additiveSelection_ = false;
    float targetZoom_ = 1.0f;
    DWORD zoomTick_ = 0;
    D2D1_POINT_2F zoomAnchorScreen_{};
    D2D1_POINT_2F zoomAnchorWorld_{};
    int activeScene_ = -1;
    int activeSection_ = -1;
    int resizeMask_ = 0;
    D2D1_POINT_2F dragWorldStart_{};
    D2D1_RECT_F originalSection_{};
    std::vector<D2D1_POINT_2F> sceneStartPositions_;
    std::map<int, D2D1_POINT_2F> carriedScenes_;
    bool gestureCheckpointed_ = false;
    std::function<void(const std::wstring&)> status_;
    std::function<void()> openSceneAction_;
    std::uint64_t frames_ = 0;
    DWORD fpsTick_ = 0;
    int fps_ = 0;
    int visibleCards_ = 0;
    int dotCount_ = 0;

    static constexpr float CardWidth = 230.0f;
    static constexpr float CardHeight = 150.0f;
    static constexpr UINT_PTR ZoomTimer = 9101;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void createResources();
    void discardResources();
    void clearTextCache();
    SceneTextCache& textCacheFor(const Scene& scene);
    void render();
    void drawGrid(ID2D1RenderTarget* target, const D2D1_SIZE_F& size);
    void drawSections(ID2D1RenderTarget* target, const D2D1_SIZE_F& size);
    void drawCards(ID2D1RenderTarget* target, const D2D1_SIZE_F& size);
    void drawLock(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, bool locked, ID2D1Brush* brush);
    void drawSelectionRectangle(ID2D1RenderTarget* target);
    void drawDiagnostics(ID2D1RenderTarget* target);
    void ensurePositions();
    D2D1_POINT_2F screenToWorld(POINT point) const;
    D2D1_POINT_2F worldToScreen(float x, float y) const;
    D2D1_RECT_F sceneRect(const Scene& scene) const;
    D2D1_RECT_F sectionRect(const BoardSection& section) const;
    Hit hitTest(POINT point) const;
    void clearSelection();
    void selectOnlyScene(int index);
    void selectOnlySection(int index);
    void beginSectionMove(int index, D2D1_POINT_2F world);
    void moveActive(D2D1_POINT_2F world);
    void finishSelection();
    void assignDraggedCardsToColumns();
    void contextMenu(POINT point);
    void openScene(int index);
    void editSceneSummary(int index);
    void deleteScene(int index, bool askConfirmation = true);
    void deleteSection(int index, bool askConfirmation = true);
    void deleteSelectedItems();
    void renameSection(int index);
    static bool contains(const D2D1_RECT_F& outer, const D2D1_RECT_F& inner);
    static bool intersects(const D2D1_RECT_F& a, const D2D1_RECT_F& b);
};

}  // namespace mezozoy::ui
