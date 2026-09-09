#pragma once

#include "CharacterEditorPanel.h"
#include "LocationEditorPanel.h"
#include "Page.h"
#include "ProjectEditorPanel.h"
#include "ReferenceEditorPanel.h"
#include "../services/ProjectClipboard.h"
#include "../services/ProjectDocument.h"

#include <memory>
#include <vector>

namespace mezozoy::ui {

class DevelopmentPage final : public NativePage {
public:
    DevelopmentPage(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme);
    void onLayout(int width, int height) override;
    void commit() override;
    bool handleCommand(int id, int code, HWND source) override;
    bool handleNotify(NMHDR* header) override;
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void applyAppearance(const Theme& theme, float uiScale) override;
    void refreshTree();
    bool copySelection(ProjectClipboard& clipboard);
    bool pasteSelection(const ProjectClipboard& clipboard);

private:
    enum class Kind { ScriptTitle, Logline, Synopsis, Character, Location, World, Reference, Relation, CharacterFolder, LocationFolder, Root };
    struct Tag { Kind kind = Kind::Root; int id = 0; };
    enum : int { IdTree = 2101, IdAdd, IdName, IdSecond, IdRole, IdDescription, IdPosterChoose, IdPosterRemove, IdCharacterOpen, IdLocationOpen };
    ProjectDocument& document_;
    HWND toolbar_{};
    HWND add_{};
    HWND tree_{};
    HWND propertyTitle_{};
    HWND nameLabel_{};
    HWND name_{};
    HWND secondLabel_{};
    HWND second_{};
    HWND roleLabel_{};
    HWND role_{};
    HWND descriptionLabel_{};
    HWND description_{};
    HWND posterLabel_{};
    HWND posterStatus_{};
    HWND posterChoose_{};
    HWND posterRemove_{};
    HWND characterOpen_{};
    HWND locationOpen_{};
    std::unique_ptr<CharacterEditorPanel> characterEditor_;
    std::unique_ptr<LocationEditorPanel> locationEditor_;
    std::unique_ptr<ProjectEditorPanel> projectEditor_;
    std::unique_ptr<ReferenceEditorPanel> referenceEditor_;
    std::vector<std::unique_ptr<Tag>> tags_;
    Tag* selected_{};
    Tag* dragging_{};
    bool loading_ = false;
    int leftWidth_ = 340;
    bool draggingSplitter_ = false;
    HTREEITEM projectTitleItem_{};
    bool scriptExpanded_ = false;
    bool charactersExpanded_ = false;
    bool locationsExpanded_ = false;
    bool worldsExpanded_ = false;
    bool referencesExpanded_ = false;
    bool relationsExpanded_ = false;
    bool characterEditorMode_ = false;
    bool locationEditorMode_ = false;
    Kind selectionAfterRefreshKind_ = Kind::Root;
    int selectionAfterRefreshId_ = 0;
    bool hasSelectionAfterRefresh_ = false;
    HTREEITEM selectionAfterRefreshItem_{};

    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;

    HTREEITEM insert(HTREEITEM parent, const std::wstring& text, Kind kind, int id, bool bold = false, bool canExpand = false);
    void rememberExpansion(const Tag& tag, bool expanded);
    void loadSelection();
    void clearProperties();
    void saveFields();
    void updateSelectedTreeCaption();
    void addMenu();
    void addCharacter();
    void addLocation();
    void addWorld();
    void addReference();
    void addRelation();
    void addFolder(const std::wstring& section);
    void contextMenu(POINT client);
    void renameSelected();
    void deleteSelected();
    void finishDrag(POINT client);
    void choosePoster();
    void removePoster();
    void updatePosterStatus();
    void selectAfterRefresh(Kind kind, int id);
    void positionDetailOpenButton();
    static LRESULT CALLBACK TreeSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void setSummarySurfaceVisible(bool visible);
    void openCharacterEditor();
    void closeCharacterEditor();
    void openReferencesForCharacter(int characterId);
    void openLocationEditor();
    void closeLocationEditor();
    Character* character(int id);
    Location* location(int id);
    WorldItem* world(int id);
    ReferenceItem* reference(int id);
    RelationshipLink* relation(int id);
    DevFolder* folder(int id);
};

}  // namespace mezozoy::ui
