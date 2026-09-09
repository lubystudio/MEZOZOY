#include "DevelopmentPage.h"

#include "Dialogs.h"
#include "Win32Util.h"
#include "UiStyle.h"
#include "../services/PosterService.h"

#include <windowsx.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mezozoy::ui {
namespace {

enum MenuId { AddCharacter = 7301, AddLocation, AddWorld, AddReference, AddRelation, AddCharacterFolder, AddLocationFolder, RenameItem, DeleteItem };

}  // namespace

DevelopmentPage::DevelopmentPage(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document) {
    toolbar_ = CreateChild(L"STATIC", L"Разработка", SS_LEFT, 0, hwnd_, instance_);
    add_ = CreateChild(L"BUTTON", L"+", BS_PUSHBUTTON | BS_FLAT, IdAdd, hwnd_, instance_);
    tree_ = CreateChild(WC_TREEVIEWW, L"", TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_NONEVENHEIGHT |
                        WS_TABSTOP | WS_CLIPSIBLINGS,
                        IdTree, hwnd_, instance_);
    propertyTitle_ = CreateChild(L"STATIC", L"Выберите элемент", SS_LEFT, 0, hwnd_, instance_);
    nameLabel_ = CreateChild(L"STATIC", L"Имя", SS_LEFT, 0, hwnd_, instance_);
    name_ = CreateChild(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IdName, hwnd_, instance_, WS_EX_CLIENTEDGE);
    secondLabel_ = CreateChild(L"STATIC", L"Настоящее имя", SS_LEFT, 0, hwnd_, instance_);
    second_ = CreateChild(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IdSecond, hwnd_, instance_, WS_EX_CLIENTEDGE);
    roleLabel_ = CreateChild(L"STATIC", L"Роль", SS_LEFT, 0, hwnd_, instance_);
    role_ = CreateChild(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IdRole, hwnd_, instance_, WS_EX_CLIENTEDGE);
    descriptionLabel_ = CreateChild(L"STATIC", L"Описание", SS_LEFT, 0, hwnd_, instance_);
    description_ = CreateChild(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, IdDescription, hwnd_, instance_, WS_EX_CLIENTEDGE);
    posterLabel_ = CreateChild(L"STATIC", L"Афиша проекта", SS_LEFT, 0, hwnd_, instance_);
    posterStatus_ = CreateChild(L"STATIC", L"Не выбрана", SS_LEFT, 0, hwnd_, instance_);
    posterChoose_ = CreateChild(L"BUTTON", L"Выбрать афишу...", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdPosterChoose, hwnd_, instance_);
    posterRemove_ = CreateChild(L"BUTTON", L"Удалить", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdPosterRemove, hwnd_, instance_);
    characterOpen_ = CreateChild(L"BUTTON", L"›", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP | WS_CLIPSIBLINGS,
                                 IdCharacterOpen, hwnd_, instance_);
    locationOpen_ = CreateChild(L"BUTTON", L"›", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP | WS_CLIPSIBLINGS,
                                IdLocationOpen, hwnd_, instance_);
    for (HWND control : {toolbar_, add_, tree_, propertyTitle_, nameLabel_, name_, secondLabel_, second_, roleLabel_, role_, descriptionLabel_, description_,
                         posterLabel_, posterStatus_, posterChoose_, posterRemove_, characterOpen_, locationOpen_})
        remember(control, control == toolbar_ || control == propertyTitle_ || control == characterOpen_ || control == locationOpen_);
    ShowWindow(characterOpen_, SW_HIDE);
    ShowWindow(locationOpen_, SW_HIDE);
    characterEditor_ = std::make_unique<CharacterEditorPanel>(instance_, hwnd_, document_, theme_);
    characterEditor_->setBackAction([this]() { closeCharacterEditor(); });
    characterEditor_->setReferencesAction([this](int characterId) { openReferencesForCharacter(characterId); });
    characterEditor_->show(false);
    locationEditor_ = std::make_unique<LocationEditorPanel>(instance_, hwnd_, document_, theme_);
    locationEditor_->setBackAction([this]() { closeLocationEditor(); });
    locationEditor_->show(false);
    projectEditor_ = std::make_unique<ProjectEditorPanel>(instance_, hwnd_, document_, theme_);
    projectEditor_->setNameChangedAction([this]() { updateSelectedTreeCaption(); });
    projectEditor_->show(false);
    referenceEditor_ = std::make_unique<ReferenceEditorPanel>(instance_, hwnd_, document_, theme_);
    referenceEditor_->setNameChangedAction([this]() { updateSelectedTreeCaption(); });
    referenceEditor_->show(false);
    TreeView_SetBkColor(tree_, theme_.navigation); TreeView_SetTextColor(tree_, theme_.text); TreeView_SetLineColor(tree_, theme_.border);
    SetWindowTheme(tree_, theme_.light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
    TreeView_SetItemHeight(tree_, style::RowHeight);
    TreeView_SetIndent(tree_, 22);
    TreeView_SetExtendedStyle(tree_, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(tree_, TreeSubclassProc, 9791, reinterpret_cast<DWORD_PTR>(this));
    document_.subscribe([this](DocumentChange change) {
        if (!IsWindow(hwnd_) || change != DocumentChange::Replaced) return;
        characterEditorMode_ = false;
        locationEditorMode_ = false;
        if (characterEditor_) characterEditor_->show(false);
        if (locationEditor_) locationEditor_->show(false);
        if (projectEditor_) projectEditor_->show(false);
        if (referenceEditor_) referenceEditor_->show(false);
        refreshTree();
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        onLayout(rect.right, rect.bottom);
    });
    refreshTree();
}

void DevelopmentPage::applyAppearance(const Theme& theme, float uiScale) {
    const bool nativeThemeChanged = theme_.light != theme.light;
    NativePage::applyAppearance(theme, uiScale);
    if (nativeThemeChanged) SetWindowTheme(tree_, theme_.light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
    TreeView_SetBkColor(tree_, theme_.navigation);
    TreeView_SetTextColor(tree_, theme_.text);
    TreeView_SetLineColor(tree_, theme_.border);
    TreeView_SetItemHeight(tree_, std::max(32, static_cast<int>(style::RowHeight * uiScale_)));
    TreeView_SetIndent(tree_, std::max(18, static_cast<int>(22.0f * uiScale_)));
    InvalidateRect(tree_, nullptr, TRUE);
    if (characterEditor_) characterEditor_->applyAppearance(theme, uiScale);
    if (locationEditor_) locationEditor_->applyAppearance(theme, uiScale);
    if (projectEditor_) projectEditor_->applyAppearance(theme, uiScale);
    if (referenceEditor_) referenceEditor_->applyAppearance(theme, uiScale);
}

bool DevelopmentPage::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON ||
        (draw->hwndItem != characterOpen_ && draw->hwndItem != locationOpen_)) {
        return false;
    }

    // This button sits inside the selected tree row, including its corner pixels.
    style::Fill(draw->hDC, draw->rcItem, theme_.selection);
    RECT face = draw->rcItem;
    InflateRect(&face, -1, -1);
    const bool hot = GetPropW(draw->hwndItem, style::Hot) != nullptr;
    style::Surface(draw->hDC, face,
        hot || (draw->itemState & ODS_SELECTED) ? Theme::Blend(theme_.selection, theme_.accent, 20) : theme_.selection,
        (draw->itemState & ODS_FOCUS) ? theme_.accent : Theme::Blend(theme_.selection, theme_.textMuted, 45));
    style::DrawIcon(draw->hDC, style::Icon::Right, style::IconBounds(face), theme_.text);
    return true;
}

void DevelopmentPage::onLayout(int width, int height) {
    if (characterEditorMode_ && characterEditor_) {
        setSummarySurfaceVisible(false);
        characterEditor_->layout(width, height);
        SetWindowPos(characterEditor_->hwnd(), HWND_TOP, 0, 0, width, height,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        return;
    }
    if (locationEditorMode_ && locationEditor_) {
        setSummarySurfaceVisible(false);
        locationEditor_->layout(width, height);
        SetWindowPos(locationEditor_->hwnd(), HWND_TOP, 0, 0, width, height,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        return;
    }
    if (characterEditor_) characterEditor_->show(false);
    if (locationEditor_) locationEditor_->show(false);
    if (projectEditor_) projectEditor_->show(false);
    if (referenceEditor_) referenceEditor_->show(false);
    setSummarySurfaceVisible(true);
    const auto scaled = [this](int value) {
        return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * uiScale_)));
    };
    leftWidth_ = std::clamp(leftWidth_, 260, std::max(260, width - 500));
    SetWindowPos(toolbar_, nullptr, scaled(16), scaled(13), leftWidth_ - scaled(64), scaled(28), SWP_NOZORDER);
    SetWindowPos(add_, nullptr, leftWidth_ - scaled(44), scaled(8), scaled(36), scaled(34), SWP_NOZORDER);
    SetWindowPos(tree_, nullptr, scaled(8), scaled(52), std::max(0, leftWidth_ - scaled(16)), std::max(0, height - scaled(60)), SWP_NOZORDER);
    if (selected_ && selected_->kind == Kind::ScriptTitle && projectEditor_) {
        const int editorX = leftWidth_ + scaled(1);
        projectEditor_->layoutAt(editorX, 0, std::max(0, width - editorX), height);
        projectEditor_->show(true);
        return;
    }
    if (projectEditor_) projectEditor_->show(false);
    if (selected_ && selected_->kind == Kind::Reference && referenceEditor_) {
        const int editorX = leftWidth_ + scaled(1);
        referenceEditor_->layoutAt(editorX, 0, std::max(0, width - editorX), height);
        referenceEditor_->show(true);
        return;
    }
    if (referenceEditor_) referenceEditor_->show(false);
    const int x = leftWidth_ + scaled(28), fieldWidth = std::max(200, width - x - scaled(24));
    SetWindowPos(propertyTitle_, nullptr, x, scaled(20), fieldWidth, scaled(28), SWP_NOZORDER);
    const int labelHeight = scaled(22), labelGap = scaled(4), editorHeight = scaled(32), rowGap = scaled(14);
    int y = scaled(60);
    auto placeRow = [&](HWND label, HWND editor) {
        SetWindowPos(label, nullptr, x, y, fieldWidth, labelHeight, SWP_NOZORDER);
        SetWindowPos(editor, nullptr, x, y + labelHeight + labelGap, fieldWidth, editorHeight, SWP_NOZORDER);
        y += labelHeight + labelGap + editorHeight + rowGap;
    };
    placeRow(nameLabel_, name_);
    placeRow(secondLabel_, second_);
    placeRow(roleLabel_, role_);
    SetWindowPos(descriptionLabel_, nullptr, x, y, fieldWidth, labelHeight, SWP_NOZORDER);
    SetWindowPos(description_, nullptr, x, y + labelHeight + labelGap, fieldWidth,
                 std::max(scaled(120), height - y - labelHeight - labelGap - scaled(28)), SWP_NOZORDER);
    SetWindowPos(posterLabel_, nullptr, x, scaled(252), fieldWidth, labelHeight, SWP_NOZORDER);
    SetWindowPos(posterStatus_, nullptr, x, scaled(276), fieldWidth, scaled(22), SWP_NOZORDER);
    SetWindowPos(posterChoose_, nullptr, x, scaled(306), scaled(190), scaled(38), SWP_NOZORDER);
    SetWindowPos(posterRemove_, nullptr, x + scaled(202), scaled(306), scaled(110), scaled(38), SWP_NOZORDER);
    positionDetailOpenButton();
}

void DevelopmentPage::commit() {
    if (characterEditorMode_ && characterEditor_) characterEditor_->commit();
    else if (locationEditorMode_ && locationEditor_) locationEditor_->commit();
    else if (selected_ && selected_->kind == Kind::ScriptTitle && projectEditor_) projectEditor_->commit();
    else if (selected_ && selected_->kind == Kind::Reference && referenceEditor_) referenceEditor_->commit();
    else saveFields();
}

HTREEITEM DevelopmentPage::insert(HTREEITEM parent, const std::wstring& text, Kind kind, int id, bool bold, bool canExpand) {
    auto tag = std::make_unique<Tag>(); tag->kind = kind; tag->id = id; Tag* pointer = tag.get(); tags_.push_back(std::move(tag));
    TVINSERTSTRUCTW item{}; item.hParent = parent; item.hInsertAfter = TVI_LAST;
    item.item.mask = TVIF_TEXT | TVIF_PARAM;
    item.item.pszText = const_cast<wchar_t*>(text.c_str());
    item.item.lParam = reinterpret_cast<LPARAM>(pointer);
    if (bold) {
        item.item.mask |= TVIF_STATE;
        item.item.stateMask = TVIS_BOLD;
        item.item.state = TVIS_BOLD;
    }
    if (canExpand) {
        item.item.mask |= TVIF_CHILDREN;
        item.item.cChildren = 1;
    }
    HTREEITEM inserted = TreeView_InsertItem(tree_, &item);
    if (hasSelectionAfterRefresh_ && kind == selectionAfterRefreshKind_ && id == selectionAfterRefreshId_)
        selectionAfterRefreshItem_ = inserted;
    return inserted;
}

void DevelopmentPage::refreshTree() {
    ScopedRedrawLock redraw(hwnd_);
    loading_ = true;
    selected_ = nullptr;
    projectTitleItem_ = nullptr;
    selectionAfterRefreshItem_ = nullptr;
    tags_.clear();
    TreeView_DeleteAllItems(tree_);

    const std::wstring projectTitle = document_.project().title.empty() ? L"Без названия" : document_.project().title;
    projectTitleItem_ = insert(TVI_ROOT, L"▤  " + projectTitle, Kind::ScriptTitle, -100, true);

    HTREEITEM people = insert(TVI_ROOT, L"♟  Персонажи", Kind::Root, -2, true, true);
    for (const auto& folderItem : document_.project().folders) if (folderItem.section == L"character") {
        HTREEITEM parent = insert(people, L"□  " + folderItem.name, Kind::CharacterFolder, folderItem.id, false, true);
        for (const auto& item : document_.project().characters) if (item.folderId == folderItem.id) insert(parent, L"♟  " + item.name, Kind::Character, item.id);
        if (folderItem.expanded) TreeView_Expand(tree_, parent, TVE_EXPAND);
    }
    for (const auto& item : document_.project().characters) if (!item.folderId) insert(people, L"♟  " + item.name, Kind::Character, item.id);

    HTREEITEM worlds = insert(TVI_ROOT, L"◈  Миры", Kind::Root, -4, true, true);
    for (const auto& item : document_.project().worlds) insert(worlds, L"◈  " + item.name, Kind::World, item.id);

    HTREEITEM places = insert(TVI_ROOT, L"⌂  Локации", Kind::Root, -3, true, true);
    for (const auto& folderItem : document_.project().folders) if (folderItem.section == L"location") {
        HTREEITEM parent = insert(places, L"□  " + folderItem.name, Kind::LocationFolder, folderItem.id, false, true);
        for (const auto& item : document_.project().locations) if (item.folderId == folderItem.id) insert(parent, L"⌂  " + item.name, Kind::Location, item.id);
        if (folderItem.expanded) TreeView_Expand(tree_, parent, TVE_EXPAND);
    }
    for (const auto& item : document_.project().locations) if (!item.folderId) insert(places, L"⌂  " + item.name, Kind::Location, item.id);

    HTREEITEM references = insert(TVI_ROOT, L"▧  Референсы", Kind::Root, -5, true, true);
    for (const auto& item : document_.project().references) insert(references, L"▧  " + item.title, Kind::Reference, item.id);

    if (charactersExpanded_) TreeView_Expand(tree_, people, TVE_EXPAND);
    if (worldsExpanded_) TreeView_Expand(tree_, worlds, TVE_EXPAND);
    if (locationsExpanded_) TreeView_Expand(tree_, places, TVE_EXPAND);
    if (referencesExpanded_) TreeView_Expand(tree_, references, TVE_EXPAND);

    HTREEITEM selectedItemHandle = selectionAfterRefreshItem_ ? selectionAfterRefreshItem_ : projectTitleItem_;
    TreeView_SelectItem(tree_, selectedItemHandle);
    if (!selected_) {
        TVITEMW selectedItem{};
        selectedItem.mask = TVIF_PARAM;
        selectedItem.hItem = selectedItemHandle;
        TreeView_GetItem(tree_, &selectedItem);
        selected_ = reinterpret_cast<Tag*>(selectedItem.lParam);
        loadSelection();
    }
    hasSelectionAfterRefresh_ = false;
    selectionAfterRefreshItem_ = nullptr;
    loading_ = false;
}

void DevelopmentPage::rememberExpansion(const Tag& tag, bool expanded) {
    if (tag.kind == Kind::Root) {
        if (tag.id == -1) scriptExpanded_ = expanded;
        else if (tag.id == -2) charactersExpanded_ = expanded;
        else if (tag.id == -3) locationsExpanded_ = expanded;
        else if (tag.id == -4) worldsExpanded_ = expanded;
        else if (tag.id == -5) referencesExpanded_ = expanded;
        else if (tag.id == -6) relationsExpanded_ = expanded;
        return;
    }
    if (tag.kind == Kind::CharacterFolder || tag.kind == Kind::LocationFolder)
        if (auto* item = folder(tag.id)) item->expanded = expanded;
}

bool DevelopmentPage::handleCommand(int id, int code, HWND) {
    if (code == 0) {
        if (id == AddCharacter) { addCharacter(); return true; }
        if (id == AddLocation) { addLocation(); return true; }
        if (id == AddWorld) { addWorld(); return true; }
        if (id == AddReference) { addReference(); return true; }
        if (id == AddRelation) { addRelation(); return true; }
        if (id == AddCharacterFolder) { addFolder(L"character"); return true; }
        if (id == AddLocationFolder) { addFolder(L"location"); return true; }
        if (id == RenameItem) { renameSelected(); return true; }
    }
    if (id == IdAdd && code == BN_CLICKED) { addMenu(); return true; }
    if (id == IdCharacterOpen && code == BN_CLICKED) { openCharacterEditor(); return true; }
    if (id == IdLocationOpen && code == BN_CLICKED) { openLocationEditor(); return true; }
    if (id == IdPosterChoose && code == BN_CLICKED) { choosePoster(); return true; }
    if (id == IdPosterRemove && code == BN_CLICKED) { removePoster(); return true; }
    if ((id == IdName || id == IdSecond || id == IdRole || id == IdDescription) && code == EN_CHANGE && !loading_) {
        saveFields();
        if (id == IdName) updateSelectedTreeCaption();
        return true;
    }
    return false;
}

bool DevelopmentPage::handleNotify(NMHDR* header) {
    if (!header || header->idFrom != IdTree) return false;
    if (header->code == TVN_KEYDOWN && reinterpret_cast<NMTVKEYDOWN*>(header)->wVKey == VK_F2) {
        // Open after the tree notification returns, outside its selection transaction.
        PostMessageW(hwnd_, WM_COMMAND, RenameItem, 0);
        return true;
    }
    if (header->code == NM_RETURN) {
        if (selected_ && selected_->kind == Kind::Character) {
            openCharacterEditor();
            return true;
        }
        if (selected_ && selected_->kind == Kind::Location) {
            openLocationEditor();
            return true;
        }
    }
    if (header->code == TVN_ITEMEXPANDEDW) {
        const auto* change = reinterpret_cast<NMTREEVIEWW*>(header);
        if (auto* tag = reinterpret_cast<Tag*>(change->itemNew.lParam))
            rememberExpansion(*tag, (change->itemNew.state & TVIS_EXPANDED) != 0);
        return true;
    }
    if (header->code == TVN_SELCHANGEDW) {
        ScopedRedrawLock redraw(hwnd_);
        saveFields();
        const auto* change = reinterpret_cast<NMTREEVIEWW*>(header);
        selected_ = reinterpret_cast<Tag*>(change->itemNew.lParam);
        loadSelection();
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        onLayout(rect.right, rect.bottom);
        return true;
    }
    if (header->code == TVN_BEGINDRAGW) {
        const auto* drag = reinterpret_cast<NMTREEVIEWW*>(header); dragging_ = reinterpret_cast<Tag*>(drag->itemNew.lParam);
        if (dragging_ && (dragging_->kind == Kind::Character || dragging_->kind == Kind::Location ||
                          dragging_->kind == Kind::CharacterFolder || dragging_->kind == Kind::LocationFolder)) {
            SetCapture(hwnd_); SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        }
        return true;
    }
    if (header->code == NM_RCLICK) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);
        contextMenu(point);
        return true;
    }
    return false;
}

LRESULT DevelopmentPage::onMessage(UINT message, WPARAM, LPARAM lParam) {
    if (message == WM_CONTEXTMENU) { POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ScreenToClient(hwnd_, &p); contextMenu(p); return 1; }
    if (message == WM_LBUTTONDOWN) { const int x = GET_X_LPARAM(lParam); if (std::abs(x - leftWidth_) <= 7) { draggingSplitter_ = true; SetCapture(hwnd_); return 1; } }
    if (message == WM_MOUSEMOVE && draggingSplitter_) { RECT r{}; GetClientRect(hwnd_, &r); leftWidth_ = std::clamp(GET_X_LPARAM(lParam), 260, static_cast<int>(r.right - 500)); onLayout(r.right, r.bottom); return 1; }
    if (message == WM_LBUTTONUP) {
        if (draggingSplitter_) { draggingSplitter_ = false; ReleaseCapture(); return 1; }
        if (dragging_) { finishDrag({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}); dragging_ = nullptr; ReleaseCapture(); SetCursor(LoadCursorW(nullptr, IDC_ARROW)); return 1; }
    }
    return 0;
}

void DevelopmentPage::loadSelection() {
    loading_ = true;
    clearProperties();
    if (!selected_) { loading_ = false; return; }
    if (selected_->kind == Kind::CharacterFolder || selected_->kind == Kind::LocationFolder) {
        SetWindowTextW(propertyTitle_, L"Папка");
        SetWindowTextW(nameLabel_, L"Название папки");
        if (auto* item = folder(selected_->id)) SetWindowTextW(name_, item->name.c_str());
        for (HWND item : {secondLabel_, second_, roleLabel_, role_, descriptionLabel_, description_}) ShowWindow(item, SW_HIDE);
        loading_ = false;
        return;
    }
    if (selected_->kind == Kind::Root) {
        SetWindowTextW(propertyTitle_, L"");
        for (HWND item : {nameLabel_, name_, secondLabel_, second_, roleLabel_, role_, descriptionLabel_, description_}) ShowWindow(item, SW_HIDE);
        loading_ = false;
        return;
    }
    if (selected_->kind == Kind::Character) {
        if (auto* item = character(selected_->id)) {
            SetWindowTextW(propertyTitle_, L"ПЕРСОНАЖ"); SetWindowTextW(nameLabel_, L"Имя персонажа"); SetWindowTextW(name_, item->name.c_str());
            SetWindowTextW(secondLabel_, L"Настоящее имя"); SetWindowTextW(second_, item->realName.c_str()); SetWindowTextW(roleLabel_, L"Роль");
            SetWindowTextW(role_, item->role.c_str()); SetWindowTextW(description_, item->description.c_str());
            ShowWindow(characterOpen_, SW_SHOW);
            positionDetailOpenButton();
        }
    } else if (selected_->kind == Kind::Location) {
        if (auto* item = location(selected_->id)) {
            SetWindowTextW(propertyTitle_, L"ЛОКАЦИЯ"); SetWindowTextW(nameLabel_, L"Название локации"); SetWindowTextW(name_, item->name.c_str());
            ShowWindow(secondLabel_, SW_HIDE); ShowWindow(second_, SW_HIDE); ShowWindow(roleLabel_, SW_HIDE); ShowWindow(role_, SW_HIDE); SetWindowTextW(description_, item->description.c_str());
            ShowWindow(locationOpen_, SW_SHOW);
            positionDetailOpenButton();
        }
    } else if (selected_->kind == Kind::World) {
        if (auto* item = world(selected_->id)) {
            SetWindowTextW(propertyTitle_, L"ЭЛЕМЕНТ МИРА"); SetWindowTextW(nameLabel_, L"Название"); SetWindowTextW(name_, item->name.c_str());
            SetWindowTextW(secondLabel_, L"Категория"); SetWindowTextW(second_, item->category.c_str()); SetWindowTextW(roleLabel_, L"Правила мира"); SetWindowTextW(role_, item->rules.c_str());
            SetWindowTextW(descriptionLabel_, L"Описание"); SetWindowTextW(description_, item->description.c_str());
        }
    } else if (selected_->kind == Kind::Reference) {
        if (auto* item = reference(selected_->id)) {
            for (HWND control : {propertyTitle_, nameLabel_, name_, secondLabel_, second_, roleLabel_, role_, descriptionLabel_, description_,
                                 posterLabel_, posterStatus_, posterChoose_, posterRemove_}) ShowWindow(control, SW_HIDE);
            if (referenceEditor_) {
                referenceEditor_->setReference(item->id);
                referenceEditor_->show(true);
            }
        }
    } else if (selected_->kind == Kind::Relation) {
        if (auto* item = relation(selected_->id)) {
            std::wstring from, to; for (const auto& characterItem : document_.project().characters) { if (characterItem.id == item->fromCharacterId) from = characterItem.name; if (characterItem.id == item->toCharacterId) to = characterItem.name; }
            SetWindowTextW(propertyTitle_, L"СВЯЗЬ ПЕРСОНАЖЕЙ"); SetWindowTextW(nameLabel_, L"От персонажа"); SetWindowTextW(name_, from.c_str());
            SetWindowTextW(secondLabel_, L"К персонажу"); SetWindowTextW(second_, to.c_str()); SetWindowTextW(roleLabel_, L"Тип связи"); SetWindowTextW(role_, item->relationType.c_str());
            SetWindowTextW(descriptionLabel_, L"Описание"); SetWindowTextW(description_, item->description.c_str()); EnableWindow(name_, FALSE); EnableWindow(second_, FALSE);
        }
    } else if (selected_->kind == Kind::ScriptTitle) {
        for (HWND item : {propertyTitle_, nameLabel_, name_, secondLabel_, second_, roleLabel_, role_, descriptionLabel_, description_,
                          posterLabel_, posterStatus_, posterChoose_, posterRemove_}) ShowWindow(item, SW_HIDE);
        if (projectEditor_) {
            projectEditor_->refresh();
            ShowWindow(projectEditor_->hwnd(), SW_SHOW);
        }
    } else if (selected_->kind == Kind::Logline || selected_->kind == Kind::Synopsis) {
        SetWindowTextW(propertyTitle_, selected_->kind == Kind::Logline ? L"ЛОГЛАЙН" : L"СИНОПСИС");
        ShowWindow(nameLabel_, SW_HIDE); ShowWindow(name_, SW_HIDE); ShowWindow(secondLabel_, SW_HIDE); ShowWindow(second_, SW_HIDE); ShowWindow(roleLabel_, SW_HIDE); ShowWindow(role_, SW_HIDE);
        SetWindowTextW(descriptionLabel_, selected_->kind == Kind::Logline ? L"Логлайн" : L"Синопсис");
        SetWindowTextW(description_, selected_->kind == Kind::Logline ? document_.project().logline.c_str() : document_.project().synopsis.c_str());
    }
    loading_ = false;
}

void DevelopmentPage::clearProperties() {
    if (projectEditor_) projectEditor_->show(false);
    if (referenceEditor_) referenceEditor_->show(false);
    for (HWND item : {nameLabel_, name_, secondLabel_, second_, roleLabel_, role_, descriptionLabel_, description_}) ShowWindow(item, SW_SHOW);
    for (HWND item : {posterLabel_, posterStatus_, posterChoose_, posterRemove_}) ShowWindow(item, SW_HIDE);
    ShowWindow(characterOpen_, SW_HIDE);
    ShowWindow(locationOpen_, SW_HIDE);
    EnableWindow(name_, TRUE); EnableWindow(second_, TRUE); EnableWindow(role_, TRUE); EnableWindow(description_, TRUE);
    SetWindowTextW(propertyTitle_, L"Выберите элемент"); SetWindowTextW(name_, L""); SetWindowTextW(second_, L""); SetWindowTextW(role_, L""); SetWindowTextW(description_, L"");
    SetWindowTextW(nameLabel_, L"Имя"); SetWindowTextW(secondLabel_, L"Дополнительно"); SetWindowTextW(roleLabel_, L"Роль"); SetWindowTextW(descriptionLabel_, L"Описание");
}

void DevelopmentPage::choosePoster() {
    if (!selected_ || selected_->kind != Kind::ScriptTitle) return;
    const auto file = OpenFileDialog(hwnd_, false,
        L"Изображения (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0PNG (*.png)\0*.png\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0Все файлы (*.*)\0*.*\0\0",
        L"png");
    if (file.empty()) return;
    Project updated = document_.project();
    std::wstring error;
    if (!PosterService::importFile(file, updated, &error)) {
        MessageBoxW(hwnd_, error.c_str(), L"Афиша проекта", MB_OK | MB_ICONERROR);
        return;
    }
    document_.checkpoint(L"Изменение афиши");
    document_.editProject().posterImageData = std::move(updated.posterImageData);
    document_.editProject().posterImageFormat = std::move(updated.posterImageFormat);
    document_.markChanged();
    updatePosterStatus();
}

void DevelopmentPage::removePoster() {
    if (!selected_ || selected_->kind != Kind::ScriptTitle || !document_.project().posterImageData) return;
    if (MessageBoxW(hwnd_, L"Удалить афишу из проекта?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    document_.checkpoint(L"Удаление афиши");
    PosterService::clear(document_.editProject());
    document_.markChanged();
    updatePosterStatus();
}

void DevelopmentPage::updatePosterStatus() {
    const bool hasPoster = static_cast<bool>(document_.project().posterImageData);
    const std::wstring status = hasPoster ? L"Афиша встроена в проект (" + document_.project().posterImageFormat + L")" : L"Афиша не выбрана";
    SetWindowTextW(posterStatus_, status.c_str());
    EnableWindow(posterRemove_, hasPoster ? TRUE : FALSE);
}

void DevelopmentPage::saveFields() {
    if (loading_ || !selected_) return;
    if (selected_->kind == Kind::ScriptTitle) {
        if (projectEditor_) projectEditor_->commit();
        return;
    }
    if (selected_->kind == Kind::Reference) {
        if (referenceEditor_) referenceEditor_->commit();
        return;
    }
    Project& project = const_cast<Project&>(document_.project());
    bool changed = false;
    auto assign = [&](std::wstring& target, const std::wstring& value) { if (target != value) { target = value; changed = true; } };
    if (selected_->kind == Kind::CharacterFolder || selected_->kind == Kind::LocationFolder) {
        if (auto* item = folder(selected_->id); item && item->name != WindowText(name_)) {
            document_.checkpoint(L"Переименование папки");
            assign(item->name, WindowText(name_));
        }
    } else if (selected_->kind == Kind::Character) {
        if (auto* item = character(selected_->id)) { assign(item->name, WindowText(name_)); assign(item->realName, WindowText(second_)); assign(item->role, WindowText(role_)); assign(item->description, WindowText(description_)); }
    } else if (selected_->kind == Kind::Location) {
        if (auto* item = location(selected_->id)) { assign(item->name, WindowText(name_)); assign(item->description, WindowText(description_)); }
    } else if (selected_->kind == Kind::World) {
        if (auto* item = world(selected_->id)) { assign(item->name, WindowText(name_)); assign(item->category, WindowText(second_)); assign(item->rules, WindowText(role_)); assign(item->description, WindowText(description_)); }
    } else if (selected_->kind == Kind::Relation) {
        if (auto* item = relation(selected_->id)) { assign(item->relationType, WindowText(role_)); assign(item->description, WindowText(description_)); }
    } else if (selected_->kind == Kind::Logline) assign(project.logline, WindowText(description_));
    else if (selected_->kind == Kind::Synopsis) assign(project.synopsis, WindowText(description_));
    if (changed) document_.markChanged();
}

void DevelopmentPage::updateSelectedTreeCaption() {
    if (!selected_) return;
    const HTREEITEM treeItem = TreeView_GetSelection(tree_);
    if (!treeItem) return;

    std::wstring caption;
    if (selected_->kind == Kind::ScriptTitle) {
        std::wstring projectTitle = document_.project().title;
        if (projectTitle.empty()) projectTitle = L"Без названия";
        caption = L"▤  " + projectTitle;
        if (projectTitleItem_) {
            TVITEMW projectItem{};
            projectItem.mask = TVIF_TEXT;
            projectItem.hItem = projectTitleItem_;
            projectItem.pszText = caption.data();
            TreeView_SetItem(tree_, &projectItem);
        }
        return;
    }
    if (selected_->kind == Kind::CharacterFolder || selected_->kind == Kind::LocationFolder) caption = L"□  " + WindowText(name_);
    else if (selected_->kind == Kind::Character) caption = L"♟  " + WindowText(name_);
    else if (selected_->kind == Kind::Location) caption = L"⌂  " + WindowText(name_);
    else if (selected_->kind == Kind::World) caption = L"◈  " + WindowText(name_);
    else if (selected_->kind == Kind::Reference) {
        const ReferenceItem* item = document_.referenceById(selected_->id);
        caption = L"▧  " + (item && !item->title.empty() ? item->title : std::wstring(L"Новый референс"));
    }
    else return;

    TVITEMW item{};
    item.mask = TVIF_TEXT;
    item.hItem = treeItem;
    item.pszText = caption.data();
    TreeView_SetItem(tree_, &item);
}

void DevelopmentPage::addMenu() {
    HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, AddCharacter, L"Персонаж"); AppendMenuW(menu, MF_STRING, AddLocation, L"Локация");
    AppendMenuW(menu, MF_STRING, AddWorld, L"Элемент мира"); AppendMenuW(menu, MF_STRING, AddReference, L"Референс");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, AddCharacterFolder, L"Папка персонажей"); AppendMenuW(menu, MF_STRING, AddLocationFolder, L"Папка локаций");
    RECT r{}; GetWindowRect(add_, &r); const int command = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.bottom, 0, hwnd_, nullptr); DestroyMenu(menu);
    if (command) handleCommand(command, 0, nullptr);
}

void DevelopmentPage::addCharacter() {
    document_.checkpoint(L"Создание персонажа");
    Character item;
    item.id = document_.project().nextId();
    item.role = L"Не установлено";
    item.profile[L"gender"] = L"Не установлено";
    const int id = item.id;
    document_.editProject().characters.push_back(std::move(item));
    document_.markChanged();
    charactersExpanded_ = true;
    selectAfterRefresh(Kind::Character, id);
    refreshTree();
    SetFocus(name_);
    SendMessageW(name_, EM_SETSEL, 0, -1);
}
void DevelopmentPage::addLocation() { document_.checkpoint(L"Создание локации"); Location item; item.id = document_.project().nextId(); const int id = item.id; document_.editProject().locations.push_back(item); document_.markChanged(); locationsExpanded_ = true; selectAfterRefresh(Kind::Location, id); refreshTree(); }
void DevelopmentPage::addWorld() { document_.checkpoint(L"Создание элемента мира"); WorldItem item; item.id = document_.project().nextId(); const int id = item.id; document_.editProject().worlds.push_back(item); document_.markChanged(); worldsExpanded_ = true; selectAfterRefresh(Kind::World, id); refreshTree(); }
void DevelopmentPage::addReference() { document_.checkpoint(L"Создание референса"); ReferenceItem item; item.id = document_.project().nextId(); const int id = item.id; document_.editProject().references.push_back(item); document_.markChanged(); referencesExpanded_ = true; selectAfterRefresh(Kind::Reference, id); refreshTree(); }
void DevelopmentPage::addRelation() {
    const auto& characters = document_.project().characters; if (characters.size() < 2) { MessageBoxW(hwnd_, L"Для связи нужны хотя бы два персонажа.", L"Mezozoy", MB_ICONINFORMATION); return; }
    const std::wstring fromName = PromptText(hwnd_, L"Новая связь", L"От персонажа", characters[0].name, theme_); if (fromName.empty()) return;
    const std::wstring toName = PromptText(hwnd_, L"Новая связь", L"К персонажу", characters[1].name, theme_); if (toName.empty()) return;
    int fromId = 0, toId = 0; for (const auto& item : characters) { if (_wcsicmp(item.name.c_str(), fromName.c_str()) == 0) fromId = item.id; if (_wcsicmp(item.name.c_str(), toName.c_str()) == 0) toId = item.id; }
    if (!fromId || !toId || fromId == toId) { MessageBoxW(hwnd_, L"Персонажи не найдены или совпадают.", L"Mezozoy", MB_ICONWARNING); return; }
    RelationshipLink item; item.id = document_.project().nextId(); item.fromCharacterId = fromId; item.toCharacterId = toId; item.relationType = PromptText(hwnd_, L"Новая связь", L"Тип связи", L"Связаны", theme_);
    document_.checkpoint(L"Создание связи персонажей");
    document_.editProject().characterRelations.push_back(std::move(item)); document_.markChanged(); refreshTree();
}
void DevelopmentPage::addFolder(const std::wstring& section) { document_.checkpoint(L"Создание папки"); DevFolder item; item.id = document_.project().nextId(); item.section = section; document_.editProject().folders.push_back(item); document_.markChanged(); refreshTree(); }

void DevelopmentPage::contextMenu(POINT client) {
    POINT treePoint = client; MapWindowPoints(hwnd_, tree_, &treePoint, 1); TVHITTESTINFO hit{}; hit.pt = treePoint; HTREEITEM item = TreeView_HitTest(tree_, &hit);
    if (!item) return; TreeView_SelectItem(tree_, item); TVITEMW info{}; info.mask = TVIF_PARAM; info.hItem = item; TreeView_GetItem(tree_, &info); selected_ = reinterpret_cast<Tag*>(info.lParam);
    if (!selected_ || selected_->kind == Kind::Root || selected_->kind == Kind::ScriptTitle || selected_->kind == Kind::Logline || selected_->kind == Kind::Synopsis) return;
    HMENU menu = CreatePopupMenu(); if (selected_->kind == Kind::CharacterFolder || selected_->kind == Kind::LocationFolder) AppendMenuW(menu, MF_STRING, RenameItem, L"Переименовать\tF2");
    AppendMenuW(menu, MF_STRING, DeleteItem, L"Удалить"); POINT screen = client; ClientToScreen(hwnd_, &screen);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD, screen.x, screen.y, 0, hwnd_, nullptr); DestroyMenu(menu);
    if (command == RenameItem) renameSelected(); else if (command == DeleteItem) deleteSelected();
}

void DevelopmentPage::renameSelected() {
    if (!selected_ || (selected_->kind != Kind::CharacterFolder && selected_->kind != Kind::LocationFolder)) return;
    const int id = selected_->id;
    const auto* item = folder(id);
    if (!item) return;
    const std::wstring name = PromptText(hwnd_, L"Переименовать папку", L"Название", item->name, theme_);
    // Reacquire after the modal loop; never retain a project pointer across it.
    if (auto* current = folder(id); current && !name.empty() && name != current->name) {
        document_.checkpoint(L"Переименование папки");
        current->name = name;
        document_.markChanged();
        loading_ = true;
        SetWindowTextW(name_, name.c_str());
        loading_ = false;
        updateSelectedTreeCaption();
    }
}

void DevelopmentPage::deleteSelected() {
    if (!selected_) return; Project& p = document_.editProject();
    document_.checkpoint(L"Удаление элемента разработки");
    if (selected_->kind == Kind::Character) p.characters.erase(std::remove_if(p.characters.begin(), p.characters.end(), [&](const Character& x) { return x.id == selected_->id; }), p.characters.end());
    else if (selected_->kind == Kind::Location) p.locations.erase(std::remove_if(p.locations.begin(), p.locations.end(), [&](const Location& x) { return x.id == selected_->id; }), p.locations.end());
    else if (selected_->kind == Kind::World) p.worlds.erase(std::remove_if(p.worlds.begin(), p.worlds.end(), [&](const WorldItem& x) { return x.id == selected_->id; }), p.worlds.end());
    else if (selected_->kind == Kind::Reference) p.references.erase(std::remove_if(p.references.begin(), p.references.end(), [&](const ReferenceItem& x) { return x.id == selected_->id; }), p.references.end());
    else if (selected_->kind == Kind::Relation) p.characterRelations.erase(std::remove_if(p.characterRelations.begin(), p.characterRelations.end(), [&](const RelationshipLink& x) { return x.id == selected_->id; }), p.characterRelations.end());
    else if (selected_->kind == Kind::CharacterFolder || selected_->kind == Kind::LocationFolder) {
        for (auto& x : p.characters) if (x.folderId == selected_->id) x.folderId = 0; for (auto& x : p.locations) if (x.folderId == selected_->id) x.folderId = 0;
        p.folders.erase(std::remove_if(p.folders.begin(), p.folders.end(), [&](const DevFolder& x) { return x.id == selected_->id; }), p.folders.end());
    }
    document_.markChanged();
    refreshTree();
}

void DevelopmentPage::finishDrag(POINT client) {
    if (!dragging_) return;
    POINT treePoint = client;
    MapWindowPoints(hwnd_, tree_, &treePoint, 1);
    TVHITTESTINFO hit{};
    hit.pt = treePoint;
    HTREEITEM targetItem = TreeView_HitTest(tree_, &hit);
    Tag* target = nullptr;
    if (targetItem) {
        TVITEMW info{};
        info.mask = TVIF_PARAM;
        info.hItem = targetItem;
        TreeView_GetItem(tree_, &info);
        target = reinterpret_cast<Tag*>(info.lParam);
    }

    auto targetSection = [&]() -> std::wstring {
        if (!target) return {};
        if (target->kind == Kind::Character || target->kind == Kind::CharacterFolder || (target->kind == Kind::Root && target->id == -2)) return L"character";
        if (target->kind == Kind::Location || target->kind == Kind::LocationFolder || (target->kind == Kind::Root && target->id == -3)) return L"location";
        return {};
    }();
    if (targetSection.empty()) return;

    int targetFolderId = 0;
    if (target->kind == Kind::CharacterFolder || target->kind == Kind::LocationFolder) targetFolderId = target->id;
    else if (target->kind == Kind::Character) { if (const auto* item = character(target->id)) targetFolderId = item->folderId; }
    else if (target->kind == Kind::Location) { if (const auto* item = location(target->id)) targetFolderId = item->folderId; }

    bool changed = false;
    Project& project = document_.editProject();
    if (dragging_->kind == Kind::Character && targetSection == L"character") {
        if (auto* item = character(dragging_->id); item && item->folderId != targetFolderId) { document_.checkpoint(L"Перемещение персонажа"); item->folderId = targetFolderId; changed = true; }
    } else if (dragging_->kind == Kind::Location && targetSection == L"location") {
        if (auto* item = location(dragging_->id); item && item->folderId != targetFolderId) { document_.checkpoint(L"Перемещение локации"); item->folderId = targetFolderId; changed = true; }
    } else if (dragging_->kind == Kind::CharacterFolder || dragging_->kind == Kind::LocationFolder) {
        const int folderId = dragging_->id;
        if (targetFolderId == folderId) return;
        const auto source = std::find_if(project.folders.begin(), project.folders.end(), [&](const DevFolder& item) { return item.id == folderId; });
        if (source == project.folders.end()) return;
        document_.checkpoint(L"Перемещение папки");
        DevFolder moved = *source;
        const bool changedSection = moved.section != targetSection;
        project.folders.erase(source);
        moved.section = targetSection;

        auto insertion = project.folders.end();
        if (targetFolderId > 0) {
            insertion = std::find_if(project.folders.begin(), project.folders.end(), [&](const DevFolder& item) { return item.id == targetFolderId; });
        } else {
            for (auto iterator = project.folders.begin(); iterator != project.folders.end(); ++iterator)
                if (iterator->section == targetSection) insertion = std::next(iterator);
        }
        project.folders.insert(insertion, std::move(moved));
        if (changedSection) {
            for (auto& item : project.characters) if (item.folderId == folderId) item.folderId = 0;
            for (auto& item : project.locations) if (item.folderId == folderId) item.folderId = 0;
        }
        changed = true;
    }
    if (changed) { document_.markChanged(); refreshTree(); }
}

bool DevelopmentPage::copySelection(ProjectClipboard& clipboard) {
    commit();
    if (!selected_) return false;
    clipboard.clear();
    switch (selected_->kind) {
        case Kind::Character:
            if (const auto* item = character(selected_->id)) { clipboard.kind = ProjectClipboardKind::Character; clipboard.character = *item; }
            break;
        case Kind::Location:
            if (const auto* item = location(selected_->id)) { clipboard.kind = ProjectClipboardKind::Location; clipboard.location = *item; }
            break;
        case Kind::World:
            if (const auto* item = world(selected_->id)) { clipboard.kind = ProjectClipboardKind::World; clipboard.world = *item; }
            break;
        case Kind::Reference:
            if (const auto* item = reference(selected_->id)) { clipboard.kind = ProjectClipboardKind::Reference; clipboard.reference = *item; }
            break;
        case Kind::Relation:
            if (const auto* item = relation(selected_->id)) { clipboard.kind = ProjectClipboardKind::Relation; clipboard.relation = *item; }
            break;
        case Kind::CharacterFolder:
        case Kind::LocationFolder:
            if (const auto* item = folder(selected_->id)) {
                clipboard.kind = ProjectClipboardKind::Folder;
                clipboard.folder = *item;
                for (const auto& characterItem : document_.project().characters) if (characterItem.folderId == item->id) clipboard.folderCharacters.push_back(characterItem);
                for (const auto& locationItem : document_.project().locations) if (locationItem.folderId == item->id) clipboard.folderLocations.push_back(locationItem);
            }
            break;
        default:
            break;
    }
    return !clipboard.empty();
}

bool DevelopmentPage::pasteSelection(const ProjectClipboard& clipboard) {
    Project& project = document_.editProject();
    if (clipboard.kind == ProjectClipboardKind::Character && clipboard.character) {
        document_.checkpoint(L"Вставка персонажа"); Character copy = *clipboard.character; copy.id = project.nextId(); copy.name += L" — копия";
        if (copy.folderId && !folder(copy.folderId)) copy.folderId = 0; project.characters.push_back(std::move(copy));
    } else if (clipboard.kind == ProjectClipboardKind::Location && clipboard.location) {
        document_.checkpoint(L"Вставка локации"); Location copy = *clipboard.location; copy.id = project.nextId(); copy.name += L" — копия";
        if (copy.folderId && !folder(copy.folderId)) copy.folderId = 0; project.locations.push_back(std::move(copy));
    } else if (clipboard.kind == ProjectClipboardKind::World && clipboard.world) {
        document_.checkpoint(L"Вставка элемента мира"); WorldItem copy = *clipboard.world; copy.id = project.nextId(); copy.name += L" — копия"; project.worlds.push_back(std::move(copy));
    } else if (clipboard.kind == ProjectClipboardKind::Reference && clipboard.reference) {
        document_.checkpoint(L"Вставка референса"); ReferenceItem copy = *clipboard.reference; copy.id = project.nextId(); copy.title += L" — копия"; project.references.push_back(std::move(copy));
    } else if (clipboard.kind == ProjectClipboardKind::Relation && clipboard.relation) {
        document_.checkpoint(L"Вставка связи персонажей"); RelationshipLink copy = *clipboard.relation; copy.id = project.nextId(); project.characterRelations.push_back(std::move(copy));
    } else if (clipboard.kind == ProjectClipboardKind::Folder && clipboard.folder) {
        document_.checkpoint(L"Вставка папки"); DevFolder copy = *clipboard.folder; const int oldId = copy.id; copy.id = project.nextId(); copy.name += L" — копия"; const int newId = copy.id; project.folders.push_back(std::move(copy));
        for (auto item : clipboard.folderCharacters) { if (item.folderId != oldId) continue; item.id = project.nextId(); item.folderId = newId; project.characters.push_back(std::move(item)); }
        for (auto item : clipboard.folderLocations) { if (item.folderId != oldId) continue; item.id = project.nextId(); item.folderId = newId; project.locations.push_back(std::move(item)); }
    } else return false;
    document_.markChanged();
    refreshTree();
    return true;
}

void DevelopmentPage::selectAfterRefresh(Kind kind, int id) {
    selectionAfterRefreshKind_ = kind;
    selectionAfterRefreshId_ = id;
    hasSelectionAfterRefresh_ = true;
}

LRESULT CALLBACK DevelopmentPage::TreeSubclassProc(HWND tree, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(tree, TreeSubclassProc, id);
    const auto result = DefSubclassProc(tree, msg, wp, lp);
    if (msg == WM_VSCROLL || msg == WM_HSCROLL || msg == WM_MOUSEWHEEL || msg == TVM_ENSUREVISIBLE)
        reinterpret_cast<DevelopmentPage*>(data)->positionDetailOpenButton();
    return result;
}

void DevelopmentPage::positionDetailOpenButton() {
    ShowWindow(characterOpen_, SW_HIDE);
    ShowWindow(locationOpen_, SW_HIDE);
    if (characterEditorMode_ || locationEditorMode_ || !selected_ ||
        (selected_->kind != Kind::Character && selected_->kind != Kind::Location)) {
        return;
    }
    HWND openButton = selected_->kind == Kind::Character ? characterOpen_ : locationOpen_;
    if (!openButton) {
        ShowWindow(characterOpen_, SW_HIDE);
        return;
    }
    HTREEITEM selectedItem = TreeView_GetSelection(tree_);
    RECT itemRect{};
    if (!selectedItem || !TreeView_GetItemRect(tree_, selectedItem, &itemRect, FALSE)) {
        return;
    }
    POINT points[2]{{itemRect.left, itemRect.top}, {itemRect.right, itemRect.bottom}};
    MapWindowPoints(tree_, hwnd_, points, 2);
    const int size = std::max(28, static_cast<int>(30.0f * uiScale_));
    RECT treeClient{};
    GetClientRect(tree_, &treeClient);
    if (itemRect.top < 0 || itemRect.bottom > treeClient.bottom) return;
    POINT rightEdge{treeClient.right, 0};
    MapWindowPoints(tree_, hwnd_, &rightEdge, 1);
    const int x = rightEdge.x - size - std::max(10, static_cast<int>(10 * uiScale_));
    const int y = static_cast<int>(points[0].y) + std::max(0, (static_cast<int>(points[1].y - points[0].y) - size) / 2);
    SetWindowPos(openButton, HWND_TOP, x, y, size, size,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    InvalidateRect(openButton, nullptr, FALSE);
}

void DevelopmentPage::setSummarySurfaceVisible(bool visible) {
    const int command = visible ? SW_SHOW : SW_HIDE;
    for (HWND control : {toolbar_, add_, tree_, propertyTitle_, nameLabel_, name_, secondLabel_, second_, roleLabel_, role_,
                         descriptionLabel_, description_, posterLabel_, posterStatus_, posterChoose_, posterRemove_, characterOpen_, locationOpen_}) {
        ShowWindow(control, command);
    }
    if (!visible && projectEditor_) projectEditor_->show(false);
    if (!visible && referenceEditor_) referenceEditor_->show(false);

    if (!visible) return;

    // loadSelection restores the context-dependent controls after the full editor is closed.
    loadSelection();
}

void DevelopmentPage::openCharacterEditor() {
    if (!selected_ || selected_->kind != Kind::Character || !characterEditor_) return;
    ScopedRedrawLock redraw(hwnd_);
    saveFields();
    characterEditorMode_ = true;
    setSummarySurfaceVisible(false);
    characterEditor_->setCharacter(selected_->id);
    RECT rect{}; GetClientRect(hwnd_, &rect);
    characterEditor_->layout(rect.right, rect.bottom);
    SetWindowPos(characterEditor_->hwnd(), HWND_TOP, 0, 0, rect.right, rect.bottom,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void DevelopmentPage::closeCharacterEditor() {
    if (!characterEditorMode_ || !characterEditor_) return;
    ScopedRedrawLock redraw(hwnd_);
    const int characterId = characterEditor_->characterId();
    characterEditor_->commit();
    characterEditorMode_ = false;
    characterEditor_->show(false);
    charactersExpanded_ = true;
    selectAfterRefresh(Kind::Character, characterId);
    refreshTree();
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void DevelopmentPage::openReferencesForCharacter(int characterId) {
    const auto found = std::find_if(document_.project().references.begin(), document_.project().references.end(),
                                    [&](const ReferenceItem& item) { return item.linkedCharacterId == characterId; });
    if (found == document_.project().references.end()) return;
    ScopedRedrawLock redraw(hwnd_);
    if (characterEditor_) characterEditor_->commit();
    characterEditorMode_ = false;
    if (characterEditor_) characterEditor_->show(false);
    referencesExpanded_ = true;
    selectAfterRefresh(Kind::Reference, found->id);
    refreshTree();
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void DevelopmentPage::openLocationEditor() {
    if (!selected_ || selected_->kind != Kind::Location || !locationEditor_) return;
    ScopedRedrawLock redraw(hwnd_);
    saveFields();
    locationEditorMode_ = true;
    setSummarySurfaceVisible(false);
    locationEditor_->setLocation(selected_->id);
    RECT rect{}; GetClientRect(hwnd_, &rect);
    locationEditor_->layout(rect.right, rect.bottom);
    SetWindowPos(locationEditor_->hwnd(), HWND_TOP, 0, 0, rect.right, rect.bottom,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void DevelopmentPage::closeLocationEditor() {
    if (!locationEditorMode_ || !locationEditor_) return;
    ScopedRedrawLock redraw(hwnd_);
    const int locationId = locationEditor_->locationId();
    locationEditor_->commit();
    locationEditorMode_ = false;
    locationEditor_->show(false);
    locationsExpanded_ = true;
    selectAfterRefresh(Kind::Location, locationId);
    refreshTree();
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

Character* DevelopmentPage::character(int id) { return document_.characterById(id); }
Location* DevelopmentPage::location(int id) { return document_.locationById(id); }
WorldItem* DevelopmentPage::world(int id) { for (auto& x : const_cast<Project&>(document_.project()).worlds) if (x.id == id) return &x; return nullptr; }
ReferenceItem* DevelopmentPage::reference(int id) { for (auto& x : const_cast<Project&>(document_.project()).references) if (x.id == id) return &x; return nullptr; }
RelationshipLink* DevelopmentPage::relation(int id) { for (auto& x : const_cast<Project&>(document_.project()).characterRelations) if (x.id == id) return &x; return nullptr; }
DevFolder* DevelopmentPage::folder(int id) { for (auto& x : const_cast<Project&>(document_.project()).folders) if (x.id == id) return &x; return nullptr; }

}  // namespace mezozoy::ui
