#pragma once

#include "Page.h"
#include "../services/ProjectDocument.h"

#include <gdiplus.h>

#include <functional>
#include <memory>

namespace mezozoy::ui {

class ReferenceEditorPanel final : public NativePage {
public:
    ReferenceEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme);
    ~ReferenceEditorPanel() override;

    void onLayout(int width, int height) override;
    void commit() override;
    bool handleCommand(int id, int code, HWND source) override;
    void applyAppearance(const Theme& theme, float uiScale) override;

    void setReference(int referenceId);
    int referenceId() const { return referenceId_; }
    void setNameChangedAction(std::function<void()> action) { nameChangedAction_ = std::move(action); }

private:
    enum : int {
        IdTitle = 8801,
        IdTag,
        IdAspect169,
        IdAspect916,
        IdAspect11,
        IdImage,
        IdImageRemove
    };

    ProjectDocument& document_;
    HWND heading_{};
    HWND titleLabel_{};
    HWND title_{};
    HWND aspectLabel_{};
    HWND aspect169_{};
    HWND aspect916_{};
    HWND aspect11_{};
    HWND tagLabel_{};
    HWND tag_{};
    HWND tagHint_{};
    HWND image_{};
    HWND imageStatus_{};
    HWND imageRemove_{};
    int referenceId_ = 0;
    bool loading_ = false;
    bool autoCompleting_ = false;
    ULONG_PTR gdiplusToken_ = 0;
    std::unique_ptr<Gdiplus::Bitmap> imageBitmap_;
    std::function<void()> nameChangedAction_;

    ReferenceItem* reference();
    const ReferenceItem* reference() const;
    void refresh();
    void saveTitle();
    void setAspect(const wchar_t* aspect);
    void rebuildTagOptions();
    void selectCurrentTag();
    void autocompleteTag();
    void applyTagSelection(int index);
    int findTagByText(const std::wstring& text, bool prefix) const;
    void chooseImage();
    void removeImage();
    void updateImage();
    void refreshImageBitmap();
    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;
};

}  // namespace mezozoy::ui
