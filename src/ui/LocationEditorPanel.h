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

class LocationEditorPanel final : public NativePage {
public:
    LocationEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme);
    ~LocationEditorPanel() override;

    void onLayout(int width, int height) override;
    void commit() override;
    bool handleCommand(int id, int code, HWND source) override;
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void applyAppearance(const Theme& theme, float uiScale) override;

    void setLocation(int locationId);
    int locationId() const { return locationId_; }
    void setBackAction(std::function<void()> action) { backAction_ = std::move(action); }
    void setNameChangedAction(std::function<void()> action) { nameChangedAction_ = std::move(action); }

private:
    struct FieldControl {
        int category = 0;
        int id = 0;
        std::wstring key;
        std::wstring label;
        HWND labelWindow{};
        HWND editor{};
        bool multiline = false;
        bool combo = false;
    };

    enum : int {
        IdBack = 8401,
        IdCategories,
        IdPhoto,
        IdFirstField = 8500
    };

    ProjectDocument& document_;
    HWND back_{};
    HWND categories_{};
    HWND sectionTitle_{};
    HWND photo_{};
    HWND locationCard_{};
    std::vector<FieldControl> fields_;
    std::function<void()> backAction_;
    std::function<void()> nameChangedAction_;
    int locationId_ = 0;
    int category_ = 0;
    int scrollOffset_ = 0;
    int contentHeight_ = 0;
    int viewportHeight_ = 0;
    bool loading_ = false;
    ULONG_PTR gdiplusToken_ = 0;
    std::unique_ptr<Gdiplus::Bitmap> photoBitmap_;

    FieldControl& addField(int category, const wchar_t* key, const wchar_t* label, bool multiline = false,
                           std::initializer_list<const wchar_t*> options = {});
    Location* location();
    const Location* location() const;
    std::wstring fieldValue(const Location& item, const std::wstring& key) const;
    void setFieldValue(Location& item, const std::wstring& key, const std::wstring& value, bool& changed);
    void loadFields();
    void saveField(const FieldControl& field);
    void showCategory(int category, bool resetScroll = true);
    void updatePhotoCaption();
    void refreshPhotoBitmap();
    void choosePhoto();
    void updateScrollBar();
    void setScrollOffset(int value);
    void forwardWheelToPage(HWND control);
    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;
};

}  // namespace mezozoy::ui
