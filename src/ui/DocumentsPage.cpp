#include "DocumentsPage.h"

#include "Dialogs.h"
#include "Win32Util.h"
#include "../core/ModelText.h"

#include <algorithm>

namespace mezozoy::ui {
namespace { constexpr int AddTypeBase = 8500; }

DocumentsPage::DocumentsPage(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document) {
    heading_ = CreateChild(L"STATIC", L"ДОКУМЕНТЫ ПРОЕКТА", SS_LEFT, 0, hwnd_, instance_);
    add_ = CreateChild(L"BUTTON", L"+ Документ", BS_PUSHBUTTON | BS_FLAT, IdAdd, hwnd_, instance_);
    remove_ = CreateChild(L"BUTTON", L"Удалить", BS_PUSHBUTTON | BS_FLAT, IdDelete, hwnd_, instance_);
    up_ = CreateChild(L"BUTTON", L"Вверх", BS_PUSHBUTTON | BS_FLAT, IdUp, hwnd_, instance_);
    down_ = CreateChild(L"BUTTON", L"Вниз", BS_PUSHBUTTON | BS_FLAT, IdDown, hwnd_, instance_);
    list_ = CreateChild(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, IdList, hwnd_, instance_, WS_EX_CLIENTEDGE);
    editorHeading_ = CreateChild(L"STATIC", L"Выберите документ", SS_LEFT, 0, hwnd_, instance_);
    titleLabel_ = CreateChild(L"STATIC", L"Название", SS_LEFT, 0, hwnd_, instance_);
    title_ = CreateChild(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IdTitle, hwnd_, instance_, WS_EX_CLIENTEDGE);
    typeLabel_ = CreateChild(L"STATIC", L"Тип", SS_LEFT, 0, hwnd_, instance_);
    type_ = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdType, hwnd_, instance_, WS_EX_CLIENTEDGE);
    bodyLabel_ = CreateChild(L"STATIC", L"Содержание", SS_LEFT, 0, hwnd_, instance_);
    body_ = CreateChild(L"EDIT", L"", ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, IdBody, hwnd_, instance_, WS_EX_CLIENTEDGE);
    for (const auto type : AllProjectDocumentTypes()) SendMessageW(type_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ProjectDocumentTypeName(type).c_str()));
    for (HWND control : {heading_, add_, remove_, up_, down_, list_, editorHeading_, titleLabel_, title_, typeLabel_, type_, bodyLabel_, body_})
        remember(control, control == heading_ || control == editorHeading_);
    document_.subscribe([this](DocumentChange change) { if (IsWindow(hwnd_) && change == DocumentChange::Replaced) refresh(); });
    refresh();
}

void DocumentsPage::onLayout(int width, int height) {
    leftWidth_ = std::clamp(leftWidth_, 280, std::max(280, width - 560));
    SetWindowPos(heading_, nullptr, 18, 16, leftWidth_ - 36, 28, SWP_NOZORDER);
    SetWindowPos(add_, nullptr, 16, 52, leftWidth_ - 32, 36, SWP_NOZORDER);
    const int half = (leftWidth_ - 44) / 2;
    SetWindowPos(up_, nullptr, 16, 96, half, 32, SWP_NOZORDER); SetWindowPos(down_, nullptr, 24 + half, 96, half, 32, SWP_NOZORDER);
    SetWindowPos(remove_, nullptr, 16, height - 48, leftWidth_ - 32, 34, SWP_NOZORDER);
    SetWindowPos(list_, nullptr, 16, 138, leftWidth_ - 32, std::max(100, height - 196), SWP_NOZORDER);
    const int x = leftWidth_ + 28, fieldWidth = std::max(260, width - x - 26);
    SetWindowPos(editorHeading_, nullptr, x, 18, fieldWidth, 28, SWP_NOZORDER);
    SetWindowPos(titleLabel_, nullptr, x, 60, fieldWidth, 20, SWP_NOZORDER); SetWindowPos(title_, nullptr, x, 82, fieldWidth, 30, SWP_NOZORDER);
    SetWindowPos(typeLabel_, nullptr, x, 124, fieldWidth, 20, SWP_NOZORDER); SetWindowPos(type_, nullptr, x, 146, std::min(360, fieldWidth), 280, SWP_NOZORDER);
    SetWindowPos(bodyLabel_, nullptr, x, 190, fieldWidth, 20, SWP_NOZORDER); SetWindowPos(body_, nullptr, x, 212, fieldWidth, std::max(120, height - 234), SWP_NOZORDER);
}

void DocumentsPage::commit() { saveSelected(); }

bool DocumentsPage::handleCommand(int id, int code, HWND) {
    if (id == IdAdd && code == BN_CLICKED) { addMenu(); return true; }
    if (id == IdDelete && code == BN_CLICKED) { deleteSelected(); return true; }
    if (id == IdUp && code == BN_CLICKED) { moveSelected(-1); return true; }
    if (id == IdDown && code == BN_CLICKED) { moveSelected(1); return true; }
    if (id == IdList && code == LBN_SELCHANGE) { saveSelected(); const int index = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0)); selectedId_ = index >= 0 ? static_cast<int>(SendMessageW(list_, LB_GETITEMDATA, index, 0)) : 0; loadSelected(); return true; }
    if ((id == IdTitle || id == IdBody) && code == EN_CHANGE && !loading_) { saveSelected(); return true; }
    if (id == IdType && code == CBN_SELCHANGE && !loading_) { saveSelected(); return true; }
    return false;
}

void DocumentsPage::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale); InvalidateRect(list_, nullptr, TRUE);
}

void DocumentsPage::refresh() {
    loading_ = true; const int previousId = selectedId_; SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    const auto& documents = document_.project().documents;
    for (const auto& item : documents) {
        std::wstring caption = ProjectDocumentTypeName(item.type) + L"  ·  " + item.title;
        if (item.parentId) caption = L"    " + caption;
        const int index = static_cast<int>(SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str())));
        SendMessageW(list_, LB_SETITEMDATA, index, item.id); if (item.id == previousId) SendMessageW(list_, LB_SETCURSEL, index, 0);
    }
    if (SendMessageW(list_, LB_GETCURSEL, 0, 0) == LB_ERR && !documents.empty()) { SendMessageW(list_, LB_SETCURSEL, 0, 0); selectedId_ = documents.front().id; }
    loading_ = false; loadSelected();
}

void DocumentsPage::addMenu() {
    HMENU menu = CreatePopupMenu(); const auto& types = AllProjectDocumentTypes();
    for (std::size_t index = 0; index < types.size(); ++index) AppendMenuW(menu, MF_STRING, AddTypeBase + static_cast<UINT>(index), ProjectDocumentTypeName(types[index]).c_str());
    RECT rect{}; GetWindowRect(add_, &rect); const int command = TrackPopupMenu(menu, TPM_RETURNCMD, rect.left, rect.bottom, 0, hwnd_, nullptr); DestroyMenu(menu);
    if (command >= AddTypeBase && command < AddTypeBase + static_cast<int>(types.size())) addDocument(types[static_cast<std::size_t>(command - AddTypeBase)]);
}

void DocumentsPage::addDocument(ProjectDocumentType type) {
    document_.checkpoint(L"Создание документа");
    Project& project = document_.editProject(); ProjectDocumentEntry item; item.id = project.nextId(); item.type = type;
    item.title = ProjectDocumentTypeName(type); item.orderIndex = static_cast<int>(project.documents.size());
    project.documents.push_back(std::move(item)); selectedId_ = project.documents.back().id; document_.markChanged(); refresh(); SetFocus(title_); SendMessageW(title_, EM_SETSEL, 0, -1);
}

void DocumentsPage::deleteSelected() {
    ProjectDocumentEntry* item = selected(); if (!item) return;
    for (const auto& scene : document_.project().scenes) if (scene.documentId == item->id) {
        MessageBoxW(hwnd_, L"В документе есть сцены. Сначала перенесите или удалите их.", L"Mezozoy", MB_ICONINFORMATION); return;
    }
    if (document_.project().documents.size() <= 1) { MessageBoxW(hwnd_, L"В проекте должен остаться хотя бы один документ.", L"Mezozoy", MB_ICONINFORMATION); return; }
    if (MessageBoxW(hwnd_, L"Удалить выбранный документ?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    document_.checkpoint(L"Удаление документа");
    Project& project = document_.editProject(); const int id = item->id;
    for (auto& child : project.documents) if (child.parentId == id) child.parentId = 0;
    project.documents.erase(std::remove_if(project.documents.begin(), project.documents.end(), [&](const auto& value) { return value.id == id; }), project.documents.end());
    selectedId_ = 0; project.normalizeOrder(); document_.markChanged(); refresh();
}

void DocumentsPage::moveSelected(int delta) {
    const int index = selectedIndex(); const int destination = index + delta; auto& items = document_.editProject().documents;
    if (index < 0 || destination < 0 || destination >= static_cast<int>(items.size())) return;
    document_.checkpoint(L"Перестановка документа");
    std::swap(items[static_cast<std::size_t>(index)], items[static_cast<std::size_t>(destination)]); document_.editProject().normalizeOrder(); document_.markChanged(); refresh();
}

bool DocumentsPage::copySelection(ProjectClipboard& clipboard) {
    saveSelected();
    ProjectDocumentEntry* item = selected();
    if (!item) return false;
    clipboard.clear(); clipboard.kind = ProjectClipboardKind::Document; clipboard.document = *item;
    return true;
}

bool DocumentsPage::pasteSelection(const ProjectClipboard& clipboard) {
    if (clipboard.kind != ProjectClipboardKind::Document || !clipboard.document) return false;
    saveSelected(); document_.checkpoint(L"Вставка документа");
    Project& project = document_.editProject(); ProjectDocumentEntry copy = *clipboard.document;
    copy.id = project.nextId(); copy.parentId = 0; copy.orderIndex = static_cast<int>(project.documents.size()); copy.title += L" — копия";
    project.documents.push_back(std::move(copy)); selectedId_ = project.documents.back().id;
    document_.markChanged(); refresh(); return true;
}

void DocumentsPage::loadSelected() {
    loading_ = true; ProjectDocumentEntry* item = selected();
    const bool enabled = item != nullptr; for (HWND control : {title_, type_, body_, remove_, up_, down_}) EnableWindow(control, enabled);
    if (!item) { SetWindowTextW(editorHeading_, L"Добавьте или выберите документ"); SetWindowTextW(title_, L""); SetWindowTextW(body_, L""); SendMessageW(type_, CB_SETCURSEL, -1, 0); loading_ = false; return; }
    SetWindowTextW(editorHeading_, item->title.c_str()); SetWindowTextW(title_, item->title.c_str()); SetWindowTextW(body_, item->text.c_str());
    const auto& types = AllProjectDocumentTypes(); const auto found = std::find(types.begin(), types.end(), item->type); SendMessageW(type_, CB_SETCURSEL, found == types.end() ? 0 : static_cast<int>(std::distance(types.begin(), found)), 0);
    loading_ = false;
}

void DocumentsPage::saveSelected() {
    if (loading_) return; ProjectDocumentEntry* item = selected(); if (!item) return; bool changed = false;
    const std::wstring title = WindowText(title_); const std::wstring body = WindowText(body_); const int typeIndex = static_cast<int>(SendMessageW(type_, CB_GETCURSEL, 0, 0));
    if (item->title != title) { item->title = title; changed = true; SetWindowTextW(editorHeading_, title.c_str()); }
    if (item->text != body) { item->text = body; item->rtf.clear(); changed = true; }
    const auto& types = AllProjectDocumentTypes(); if (typeIndex >= 0 && typeIndex < static_cast<int>(types.size()) && item->type != types[static_cast<std::size_t>(typeIndex)]) { item->type = types[static_cast<std::size_t>(typeIndex)]; changed = true; }
    if (changed) document_.markChanged();
}

ProjectDocumentEntry* DocumentsPage::selected() { for (auto& item : document_.editProject().documents) if (item.id == selectedId_) return &item; return nullptr; }
int DocumentsPage::selectedIndex() const { const auto& items = document_.project().documents; for (std::size_t index = 0; index < items.size(); ++index) if (items[index].id == selectedId_) return static_cast<int>(index); return -1; }

}  // namespace mezozoy::ui
