#pragma once

#include "Page.h"
#include "../services/ProjectClipboard.h"
#include "../services/ProjectDocument.h"

namespace mezozoy::ui {

class DocumentsPage final : public NativePage {
public:
    DocumentsPage(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme);
    void onLayout(int width, int height) override;
    void commit() override;
    bool handleCommand(int id, int code, HWND source) override;
    void applyAppearance(const Theme& theme, float uiScale) override;
    void refresh();
    bool copySelection(ProjectClipboard& clipboard);
    bool pasteSelection(const ProjectClipboard& clipboard);

private:
    enum : int { IdList = 5101, IdAdd, IdDelete, IdUp, IdDown, IdTitle, IdType, IdBody };
    ProjectDocument& document_;
    HWND heading_{};
    HWND add_{};
    HWND remove_{};
    HWND up_{};
    HWND down_{};
    HWND list_{};
    HWND editorHeading_{};
    HWND titleLabel_{};
    HWND title_{};
    HWND typeLabel_{};
    HWND type_{};
    HWND bodyLabel_{};
    HWND body_{};
    int selectedId_ = 0;
    bool loading_ = false;
    int leftWidth_ = 340;

    void addMenu();
    void addDocument(ProjectDocumentType type);
    void deleteSelected();
    void moveSelected(int delta);
    void loadSelected();
    void saveSelected();
    ProjectDocumentEntry* selected();
    int selectedIndex() const;
};

}  // namespace mezozoy::ui
