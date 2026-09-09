#pragma once

#include "Page.h"
#include "../services/ProjectDocument.h"

#include <gdiplus.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mezozoy::ui {

class CharacterEditorPanel final : public NativePage {
public:
    CharacterEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme);
    ~CharacterEditorPanel() override;

    void onLayout(int width, int height) override;
    void commit() override;
    bool handleCommand(int id, int code, HWND source) override;
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void applyAppearance(const Theme& theme, float uiScale) override;

    void setCharacter(int characterId);
    int characterId() const { return characterId_; }
    void setBackAction(std::function<void()> action) { backAction_ = std::move(action); }
    void setNameChangedAction(std::function<void()> action) { nameChangedAction_ = std::move(action); }
    void setReferencesAction(std::function<void(int)> action) { referencesAction_ = std::move(action); }

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
        IdBack = 8201,
        IdCategories,
        IdPhoto,
        IdReferences,
        IdFirstField = 8300
    };

    ProjectDocument& document_;
    HWND back_{};
    HWND categories_{};
    HWND sectionTitle_{};
    HWND photo_{};
    HWND characterCard_{};
    HWND references_{};
    std::vector<FieldControl> fields_;
    std::function<void()> backAction_;
    std::function<void()> nameChangedAction_;
    std::function<void(int)> referencesAction_;
    int characterId_ = 0;
    int category_ = 0;
    int scrollOffset_ = 0;
    int contentHeight_ = 0;
    int viewportHeight_ = 0;
    bool loading_ = false;
    ULONG_PTR gdiplusToken_ = 0;
    std::unique_ptr<Gdiplus::Bitmap> photoBitmap_;

    FieldControl& addField(int category, const wchar_t* key, const wchar_t* label, bool multiline = false,
                           std::initializer_list<const wchar_t*> options = {});
    Character* character();
    const Character* character() const;
    std::wstring fieldValue(const Character& item, const std::wstring& key) const;
    void setFieldValue(Character& item, const std::wstring& key, const std::wstring& value, bool& changed);
    void loadFields();
    void saveField(const FieldControl& field);
    void showCategory(int category, bool resetScroll = true);
    void updatePhotoCaption();
    void refreshPhotoBitmap();
    void choosePhoto();
    std::size_t linkedReferenceCount() const;
    void updateScrollBar();
    void setScrollOffset(int value);
    void forwardWheelToPage(HWND control);
    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;
};

}  // namespace mezozoy::ui
