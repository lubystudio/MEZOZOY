#pragma once

#include "Page.h"
#include "../services/ProjectDocument.h"

#include <gdiplus.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace mezozoy::ui {

class ProjectEditorPanel final : public NativePage {
public:
    ProjectEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme);
    ~ProjectEditorPanel() override;

    void onLayout(int width, int height) override;
    void commit() override;
    bool handleCommand(int id, int code, HWND source) override;
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void applyAppearance(const Theme& theme, float uiScale) override;

    void refresh();
    void setNameChangedAction(std::function<void()> action) { nameChangedAction_ = std::move(action); }

private:
    struct FieldControl {
        int id = 0;
        std::wstring key;
        HWND label{};
        HWND editor{};
        int row = 0;
        int column = -1;
        bool multiline = false;
        bool combo = false;
    };

    enum : int {
        IdPoster = 8601,
        IdPosterRemove,
        IdFirstField = 8700
    };

    ProjectDocument& document_;
    HWND heading_{};
    HWND poster_{};
    HWND posterStatus_{};
    HWND posterRemove_{};
    std::vector<FieldControl> fields_;
    std::function<void()> nameChangedAction_;
    bool loading_ = false;
    int scrollOffset_ = 0;
    int contentHeight_ = 0;
    int viewportHeight_ = 0;
    ULONG_PTR gdiplusToken_ = 0;
    std::unique_ptr<Gdiplus::Bitmap> posterBitmap_;

    FieldControl& addField(const wchar_t* key, const wchar_t* label, int row, int column = -1,
                           bool multiline = false, std::initializer_list<const wchar_t*> options = {});
    std::wstring fieldValue(const Project& project, const std::wstring& key) const;
    void setFieldValue(Project& project, const std::wstring& key, const std::wstring& value, bool& changed);
    void saveField(const FieldControl& field);
    void choosePoster();
    void removePoster();
    void updatePoster();
    void refreshPosterBitmap();
    void updateScrollBar();
    void setScrollOffset(int value);
    void forwardWheelToPage(HWND control);
    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;
};

}  // namespace mezozoy::ui
