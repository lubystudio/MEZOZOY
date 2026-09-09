#include "CharacterEditorPanel.h"

#include "Win32Util.h"
#include "../services/PosterService.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace mezozoy::ui {
namespace {

constexpr UINT_PTR WheelForwardSubclass = 0x4D5A4348;

Gdiplus::Color Color(COLORREF value, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(value), GetGValue(value), GetBValue(value));
}

std::unique_ptr<Gdiplus::Bitmap> CloneBitmap(Gdiplus::Bitmap* source) {
    if (!source || source->GetLastStatus() != Gdiplus::Ok || source->GetWidth() == 0 || source->GetHeight() == 0) return {};
    constexpr float maximumPreviewSide = 1024.0f;
    const float sourceWidth = static_cast<float>(source->GetWidth());
    const float sourceHeight = static_cast<float>(source->GetHeight());
    const float scale = std::min(1.0f, maximumPreviewSide / std::max(sourceWidth, sourceHeight));
    const UINT width = std::max(1U, static_cast<UINT>(std::lround(sourceWidth * scale)));
    const UINT height = std::max(1U, static_cast<UINT>(std::lround(sourceHeight * scale)));
    auto result = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(result.get());
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(source, 0, 0, width, height);
    return result;
}

std::unique_ptr<Gdiplus::Bitmap> BitmapFromBytes(const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return {};
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return {};
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return {};
    }
    std::copy(bytes.begin(), bytes.end(), static_cast<unsigned char*>(target));
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return {};
    }
    std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromStream(stream, FALSE));
    auto result = CloneBitmap(source.get());
    source.reset();
    stream->Release();
    return result;
}

LRESULT CALLBACK ForwardMouseWheel(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR subclassId, DWORD_PTR reference) {
    if (message == WM_MOUSEWHEEL) {
        SendMessageW(reinterpret_cast<HWND>(reference), message, wParam, lParam);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ForwardMouseWheel, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

constexpr const wchar_t* CategoryNames[] = {
    L"Главное", L"История", L"Личные данные", L"Внешность", L"Жизнь", L"Мироощущение", L"Биография"
};

constexpr const wchar_t* CategoryRows[] = {
    L"▣   Главное", L"⚿   История", L"☷   Личные данные", L"◒   Внешность", L"♨   Жизнь", L"●   Мироощущение", L"✎   Биография"
};

}  // namespace

CharacterEditorPanel::CharacterEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document) {
    Gdiplus::GdiplusStartupInput startup;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &startup, nullptr);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, GetWindowLongPtrW(hwnd_, GWL_STYLE) | WS_VSCROLL);

    back_ = CreateChild(L"BUTTON", L"‹  ПЕРСОНАЖ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdBack, hwnd_, instance_);
    categories_ = CreateChild(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP,
                              IdCategories, hwnd_, instance_, WS_EX_CLIENTEDGE);
    SetPropW(categories_,L"Mezozoy.Ui.List",reinterpret_cast<HANDLE>(1));
    for (const wchar_t* row : CategoryRows) SendMessageW(categories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
    sectionTitle_ = CreateChild(L"STATIC", L"ГЛАВНОЕ", SS_LEFT, 0, hwnd_, instance_);
    photo_ = CreateChild(L"BUTTON", L"+  ДОБАВИТЬ ФОТО", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdPhoto, hwnd_, instance_);
    characterCard_ = CreateChild(L"STATIC", L"ПЕРСОНАЖ", SS_LEFT, 0, hwnd_, instance_);
    references_ = CreateChild(L"BUTTON", L"→  ПЕРЕЙТИ К РЕФЕРЕНСАМ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP,
                              IdReferences, hwnd_, instance_);
    for (HWND control : {back_, categories_, sectionTitle_, photo_, characterCard_, references_})
        remember(control, control == sectionTitle_ || control == characterCard_);
    ShowWindow(references_, SW_HIDE);

    addField(0, L"name", L"Название");
    addField(0, L"role", L"Роль в истории", false,
             {L"Не установлено", L"Главный герой", L"Антагонист", L"Второстепенный персонаж", L"Эпизодический персонаж"});
    addField(0, L"age", L"Возраст");
    addField(0, L"gender", L"Пол", false, {L"Не установлено", L"Мужской", L"Женский", L"Другой"});
    addField(0, L"shortDescription", L"Краткое описание");
    addField(0, L"description", L"Подробное описание", true);
    addField(0, L"dreamcast", L"Дримкаст");

    addField(1, L"goal", L"Внешняя цель");
    addField(1, L"motivation", L"Внутренняя цель");
    addField(1, L"initialBeliefs", L"Исходные убеждения");
    addField(1, L"changedBeliefs", L"Изменённые убеждения");
    addField(1, L"changeTrigger", L"Что приводит к изменениям");
    addField(1, L"firstAppearance", L"Первое появление");
    addField(1, L"storyInvolvement", L"Вовлечённость в сюжет");
    addField(1, L"conflict", L"Конфликт");
    addField(1, L"decisiveMoment", L"Решающий момент");
    addField(1, L"arc", L"Арка персонажа", true);

    addField(2, L"realName", L"Настоящее имя");
    addField(2, L"aliases", L"Псевдонимы / другие имена");
    addField(2, L"birthDate", L"Дата рождения");
    addField(2, L"birthPlace", L"Место рождения");
    addField(2, L"ethnicity", L"Этнос / раса");
    addField(2, L"maritalStatus", L"Семейное положение");

    addField(3, L"appearance", L"Общее описание внешности", true);
    addField(3, L"height", L"Рост");
    addField(3, L"weight", L"Вес");
    addField(3, L"bodyType", L"Телосложение");
    addField(3, L"skinColor", L"Цвет кожи");
    addField(3, L"hairstyle", L"Причёска");
    addField(3, L"hairColor", L"Цвет волос");
    addField(3, L"eyeShape", L"Форма глаз");
    addField(3, L"eyeColor", L"Цвет глаз");
    addField(3, L"faceShape", L"Форма лица");
    addField(3, L"distinctiveFeatures", L"Отличительные черты внешности");
    addField(3, L"otherFaceFeatures", L"Прочие особенности лица");
    addField(3, L"posture", L"Осанка");
    addField(3, L"otherAppearance", L"Прочие особенности внешности", true);

    addField(4, L"skills", L"Навыки");
    addField(4, L"skillsOrigin", L"Как получены навыки");
    addField(4, L"incompetence", L"Некомпетентность");
    addField(4, L"strengthTalent", L"Сила / талант");
    addField(4, L"weakness", L"Слабость");
    addField(4, L"hobbies", L"Увлечения");
    addField(4, L"habits", L"Привычки");
    addField(4, L"health", L"Здоровье");
    addField(4, L"speech", L"Речь");
    addField(4, L"pet", L"Домашнее животное");
    addField(4, L"clothing", L"Одежда");
    addField(4, L"specialInterests", L"Особые интересы / оружие / приспособления");
    addField(4, L"accessories", L"Аксессуары");
    addField(4, L"livingEnvironment", L"Место проживания / среда");
    addField(4, L"homeDescription", L"Описание дома", true);
    addField(4, L"neighbors", L"Соседи");
    addField(4, L"organizations", L"Участие в организациях");
    addField(4, L"income", L"Доход");
    addField(4, L"occupation", L"Работа / род действий");
    addField(4, L"jobTitle", L"Должность на работе");
    addField(4, L"jobSatisfaction", L"Удовлетворённость работой");

    addField(5, L"personality", L"Черты характера");
    addField(5, L"moralBeliefs", L"Моральные установки");
    addField(5, L"drivingForce", L"Движущая сила");
    addField(5, L"depressionTrigger", L"Что приводит к упадку духа");
    addField(5, L"philosophy", L"Философские взгляды");
    addField(5, L"greatestFear", L"Самый сильный страх");
    addField(5, L"selfControl", L"Самоконтроль");
    addField(5, L"intelligence", L"Уровень интеллекта");
    addField(5, L"confidence", L"Уровень уверенности");

    addField(6, L"backstory", L"Детство / предыстория", true);
    addField(6, L"pastKeyEvent", L"Важное событие из прошлого", true);
    addField(6, L"greatestAchievement", L"Лучшее достижение");
    addField(6, L"otherAchievements", L"Другие достижения");
    addField(6, L"worstMoment", L"Худший момент", true);
    addField(6, L"failures", L"Неудачи", true);
    addField(6, L"secrets", L"Секреты", true);
    addField(6, L"bestMemories", L"Лучшие воспоминания", true);
    addField(6, L"worstMemories", L"Худшие воспоминания", true);

    SendMessageW(categories_, LB_SETCURSEL, 0, 0);
    showCategory(0);
}

CharacterEditorPanel::~CharacterEditorPanel() {
    photoBitmap_.reset();
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

CharacterEditorPanel::FieldControl& CharacterEditorPanel::addField(
    int category, const wchar_t* key, const wchar_t* label, bool multiline,
    std::initializer_list<const wchar_t*> options) {
    FieldControl field;
    field.category = category;
    field.id = IdFirstField + static_cast<int>(fields_.size());
    field.key = key;
    field.label = label;
    field.multiline = multiline;
    field.combo = options.size() > 0;
    field.labelWindow = CreateChild(L"STATIC", label, SS_LEFT, 0, hwnd_, instance_);
    if (field.combo) {
        field.editor = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
                                   field.id, hwnd_, instance_, WS_EX_CLIENTEDGE);
        for (const wchar_t* option : options) SendMessageW(field.editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option));
    } else {
        DWORD style = WS_TABSTOP | (multiline ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL);
        field.editor = CreateChild(L"EDIT", L"", style, field.id, hwnd_, instance_, WS_EX_CLIENTEDGE);
    }
    remember(field.labelWindow);
    remember(field.editor);
    forwardWheelToPage(field.editor);
    fields_.push_back(field);
    return fields_.back();
}

void CharacterEditorPanel::forwardWheelToPage(HWND control) {
    SetWindowSubclass(control, ForwardMouseWheel, WheelForwardSubclass, reinterpret_cast<DWORD_PTR>(hwnd_));
}

Character* CharacterEditorPanel::character() {
    return document_.characterById(characterId_);
}

const Character* CharacterEditorPanel::character() const {
    return document_.characterById(characterId_);
}

std::wstring CharacterEditorPanel::fieldValue(const Character& item, const std::wstring& key) const {
    if (key == L"name") return item.name;
    if (key == L"realName") return item.realName;
    if (key == L"aliases") return item.aliases;
    if (key == L"role") return item.role;
    if (key == L"description") return item.description;
    if (key == L"shortDescription") return item.shortDescription;
    if (key == L"age") return item.age;
    if (key == L"appearance") return item.appearance;
    if (key == L"personality") return item.personality;
    if (key == L"motivation") return item.motivation;
    if (key == L"goal") return item.goal;
    if (key == L"conflict") return item.conflict;
    if (key == L"backstory") return item.backstory;
    if (key == L"arc") return item.arc;
    const auto found = item.profile.find(key);
    return found == item.profile.end() ? std::wstring{} : found->second;
}

void CharacterEditorPanel::setFieldValue(Character& item, const std::wstring& key, const std::wstring& value, bool& changed) {
    auto assign = [&](std::wstring& target) { if (target != value) { target = value; changed = true; } };
    if (key == L"name") assign(item.name);
    else if (key == L"realName") assign(item.realName);
    else if (key == L"aliases") assign(item.aliases);
    else if (key == L"role") assign(item.role);
    else if (key == L"description") assign(item.description);
    else if (key == L"shortDescription") assign(item.shortDescription);
    else if (key == L"age") assign(item.age);
    else if (key == L"appearance") assign(item.appearance);
    else if (key == L"personality") assign(item.personality);
    else if (key == L"motivation") assign(item.motivation);
    else if (key == L"goal") assign(item.goal);
    else if (key == L"conflict") assign(item.conflict);
    else if (key == L"backstory") assign(item.backstory);
    else if (key == L"arc") assign(item.arc);
    else {
        const auto found = item.profile.find(key);
        const std::wstring current = found == item.profile.end() ? std::wstring{} : found->second;
        if (current == value) return;
        if (value.empty()) item.profile.erase(key); else item.profile[key] = value;
        changed = true;
    }
}

void CharacterEditorPanel::setCharacter(int characterId) {
    ScopedRedrawLock redraw(hwnd_);
    if (characterId_ && characterId_ != characterId) commit();
    characterId_ = characterId;
    category_ = 0;
    scrollOffset_ = 0;
    loadFields();
    showCategory(0);
}

void CharacterEditorPanel::loadFields() {
    loading_ = true;
    const Character* item = character();
    for (const auto& field : fields_) SetWindowTextW(field.editor, item ? fieldValue(*item, field.key).c_str() : L"");
    const std::wstring name = item && !item->name.empty() ? item->name : L"ПЕРСОНАЖ";
    SetWindowTextW(back_, (L"‹  " + name).c_str());
    updatePhotoCaption();
    loading_ = false;
}

void CharacterEditorPanel::saveField(const FieldControl& field) {
    if (loading_) return;
    const std::wstring value = WindowText(field.editor);
    const bool updated = document_.updateCharacter(characterId_, [&](Character& item) {
        bool changed = false;
        setFieldValue(item, field.key, value, changed);
        return changed;
    });
    if (!updated) return;
    if (field.key == L"name") {
        const Character* item = character();
        const std::wstring name = !item || item->name.empty() ? L"ПЕРСОНАЖ" : item->name;
        SetWindowTextW(back_, (L"‹  " + name).c_str());
        if (nameChangedAction_) nameChangedAction_();
    }
}

void CharacterEditorPanel::commit() {
    if (loading_) return;
    for (const auto& field : fields_) saveField(field);
}

bool CharacterEditorPanel::handleCommand(int id, int code, HWND) {
    if (id == IdBack && code == BN_CLICKED) {
        commit();
        if (backAction_) backAction_();
        return true;
    }
    if (id == IdCategories && code == LBN_SELCHANGE) {
        commit();
        int selected = static_cast<int>(SendMessageW(categories_, LB_GETCURSEL, 0, 0));
        if (selected < 0) selected = 0;
        showCategory(selected);
        return true;
    }
    if (id == IdPhoto && code == BN_CLICKED) { choosePhoto(); return true; }
    if (id == IdReferences && code == BN_CLICKED) {
        commit();
        if (referencesAction_) referencesAction_(characterId_);
        return true;
    }
    if (id >= IdFirstField && id < IdFirstField + static_cast<int>(fields_.size())) {
        const auto& field = fields_[static_cast<std::size_t>(id - IdFirstField)];
        if ((!field.combo && code == EN_CHANGE) || (field.combo && (code == CBN_EDITCHANGE || code == CBN_SELCHANGE))) {
            saveField(field);
            return true;
        }
    }
    return false;
}

void CharacterEditorPanel::showCategory(int category, bool resetScroll) {
    ScopedRedrawLock redraw(hwnd_);
    category_ = std::clamp(category, 0, static_cast<int>(std::size(CategoryNames)) - 1);
    if (resetScroll) scrollOffset_ = 0;
    loading_ = true;
    SendMessageW(categories_, LB_SETCURSEL, category_, 0);
    SetWindowTextW(sectionTitle_, CategoryNames[category_]);
    for (const auto& field : fields_) {
        ShowWindow(field.labelWindow, SW_HIDE);
        ShowWindow(field.editor, SW_HIDE);
    }
    loading_ = false;
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void CharacterEditorPanel::onLayout(int width, int height) {
    const auto scaled = [this](int value) {
        return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * uiScale_)));
    };
    const int navigationWidth = std::clamp(static_cast<int>(300.0f * uiScale_), 250, std::max(250, width / 3));
    const int navigationHeaderHeight = scaled(54);
    SetWindowPos(back_, nullptr, 0, 0, navigationWidth, navigationHeaderHeight, SWP_NOZORDER);
    SetWindowPos(categories_, nullptr, 0, navigationHeaderHeight, navigationWidth,
                 std::max(0, height - navigationHeaderHeight), SWP_NOZORDER);

    const int contentX = navigationWidth + scaled(30);
    const int contentRight = scaled(28);
    const bool showPhoto = width - contentX >= scaled(820);
    const int photoWidth = showPhoto ? scaled(240) : 0;
    const int photoGap = showPhoto ? scaled(26) : 0;
    const int fieldWidth = std::max(260, width - contentX - contentRight - photoWidth - photoGap);
    SetWindowPos(sectionTitle_, nullptr, contentX, scaled(18), fieldWidth, scaled(34), SWP_NOZORDER);

    if (showPhoto) {
        const int photoX = width - contentRight - photoWidth;
        const bool hasReferences = linkedReferenceCount() > 0;
        ShowWindow(photo_, SW_SHOW);
        ShowWindow(characterCard_, SW_SHOW);
        ShowWindow(references_, hasReferences ? SW_SHOW : SW_HIDE);
        SetWindowPos(photo_, nullptr, photoX, scaled(72), photoWidth, scaled(190), SWP_NOZORDER);
        SetWindowPos(characterCard_, nullptr, photoX + scaled(12), scaled(286), photoWidth - scaled(24), scaled(90), SWP_NOZORDER);
        SetWindowPos(references_, nullptr, photoX, scaled(394), photoWidth, scaled(42), SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        ShowWindow(photo_, SW_HIDE);
        ShowWindow(characterCard_, SW_HIDE);
        ShowWindow(references_, SW_HIDE);
    }

    const int labelHeight = scaled(22);
    const int labelGap = scaled(4);
    const int rowGap = scaled(16);
    const int singleLineHeight = scaled(38);
    const int multilineHeight = scaled(82);
    int y = scaled(70) - scrollOffset_;
    const int topClip = scaled(58);
    const int bottomClip = height;
    for (const auto& field : fields_) {
        if (field.category != category_) continue;
        const int visualHeight = field.multiline ? multilineHeight : singleLineHeight;
        const int editHeight = field.combo ? std::max(scaled(220), visualHeight) : visualHeight;
        const bool visible = y >= topClip && y < bottomClip;
        const UINT visibility = visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
        constexpr UINT layoutFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;
        SetWindowPos(field.labelWindow, nullptr, contentX, y, fieldWidth, labelHeight, layoutFlags | visibility);
        SetWindowPos(field.editor, nullptr, contentX, y + labelHeight + labelGap, fieldWidth, editHeight,
                     layoutFlags | visibility);
        y += labelHeight + labelGap + visualHeight + rowGap;
    }
    contentHeight_ = y + scrollOffset_ + scaled(28);
    viewportHeight_ = std::max(1, height - topClip);
    updateScrollBar();

    if (const Character* item = character()) {
        std::wstring gender = fieldValue(*item, L"gender");
        if (gender.empty()) gender = L"Не установлено";
        std::wstring role = item->role.empty() ? L"Не установлено" : item->role;
        const std::wstring caption = (item->name.empty() ? L"ПЕРСОНАЖ" : item->name) + L"\r\nПол: " + gender + L"\r\nРоль: " + role;
        SetWindowTextW(characterCard_, caption.c_str());
    }
}

void CharacterEditorPanel::updateScrollBar() {
    const int maximum = std::max(0, contentHeight_ - viewportHeight_);
    scrollOffset_ = std::clamp(scrollOffset_, 0, maximum);
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, contentHeight_ - 1);
    info.nPage = static_cast<UINT>(viewportHeight_);
    info.nPos = scrollOffset_;
    SetScrollInfo(hwnd_, SB_VERT, &info, TRUE);
    ShowScrollBar(hwnd_, SB_VERT, maximum > 0 ? TRUE : FALSE);
}

void CharacterEditorPanel::setScrollOffset(int value) {
    const int maximum = std::max(0, contentHeight_ - viewportHeight_);
    const int next = std::clamp(value, 0, maximum);
    if (next == scrollOffset_) return;
    ScopedRedrawLock redraw(hwnd_);
    scrollOffset_ = next;
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

LRESULT CharacterEditorPanel::onMessage(UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_VSCROLL) {
        SCROLLINFO info{}; info.cbSize = sizeof(info); info.fMask = SIF_ALL; GetScrollInfo(hwnd_, SB_VERT, &info);
        int next = scrollOffset_;
        switch (LOWORD(wParam)) {
            case SB_LINEUP: next -= 48; break;
            case SB_LINEDOWN: next += 48; break;
            case SB_PAGEUP: next -= viewportHeight_; break;
            case SB_PAGEDOWN: next += viewportHeight_; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: next = info.nTrackPos; break;
            case SB_TOP: next = 0; break;
            case SB_BOTTOM: next = std::max(0, contentHeight_ - viewportHeight_); break;
            default: return 1;
        }
        setScrollOffset(next);
        return 1;
    }
    if (message == WM_MOUSEWHEEL) {
        const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        setScrollOffset(scrollOffset_ - steps * 72);
        return 1;
    }
    return 0;
}

void CharacterEditorPanel::choosePhoto() {
    const Character* item = character();
    if (!item) return;
    const auto path = OpenFileDialog(hwnd_, false,
        L"Изображения (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Все файлы (*.*)\0*.*\0\0", L"png");
    if (path.empty()) return;

    Character imported = *item;
    std::wstring error;
    if (!PosterService::importFile(path, imported, &error)) {
        MessageBoxW(hwnd_, error.c_str(), L"Фотография персонажа", MB_OK | MB_ICONERROR);
        return;
    }
    const auto importedData = imported.avatarImageData;
    const std::wstring importedFormat = imported.avatarImageFormat;
    const std::wstring importedName = imported.avatarPath;
    document_.updateCharacter(characterId_, [&](Character& target) {
        const std::wstring oldData = target.avatarImageData ? *target.avatarImageData : std::wstring{};
        const std::wstring newData = importedData ? *importedData : std::wstring{};
        if (oldData == newData && target.avatarImageFormat == importedFormat && target.avatarPath == importedName) return false;
        target.avatarImageData = importedData;
        target.avatarImageFormat = importedFormat;
        target.avatarPath = importedName;
        return true;
    }, L"Изменение фотографии персонажа");
    updatePhotoCaption();
}

std::size_t CharacterEditorPanel::linkedReferenceCount() const {
    return static_cast<std::size_t>(std::count_if(document_.project().references.begin(), document_.project().references.end(),
        [&](const ReferenceItem& item) { return item.linkedCharacterId == characterId_; }));
}

void CharacterEditorPanel::updatePhotoCaption() {
    const Character* item = character();
    std::wstring caption = L"+  ДОБАВИТЬ ФОТО";
    if (item && (item->avatarImageData || !item->avatarPath.empty())) caption = L"✎  ИЗМЕНИТЬ ФОТО";
    SetWindowTextW(photo_, caption.c_str());
    refreshPhotoBitmap();
    InvalidateRect(photo_, nullptr, TRUE);
}

void CharacterEditorPanel::refreshPhotoBitmap() {
    photoBitmap_.reset();
    const Character* item = character();
    if (!item || !gdiplusToken_) return;
    photoBitmap_ = BitmapFromBytes(PosterService::decode(*item));
    if (photoBitmap_ || item->avatarPath.empty()) return;

    std::error_code error;
    const std::filesystem::path legacyPath(item->avatarPath);
    if (!std::filesystem::is_regular_file(legacyPath, error)) return;
    Gdiplus::Bitmap source(legacyPath.c_str());
    photoBitmap_ = CloneBitmap(&source);
}

bool CharacterEditorPanel::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON || draw->hwndItem != photo_) return false;
    const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
    const bool focused = (draw->itemState & ODS_FOCUS) != 0;
    Gdiplus::Graphics graphics(draw->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    const float width = static_cast<float>(draw->rcItem.right - draw->rcItem.left);
    const float height = static_cast<float>(draw->rcItem.bottom - draw->rcItem.top);
    Gdiplus::RectF bounds(0.0f, 0.0f, width, height);
    Gdiplus::SolidBrush background(Color(pressed ? theme_.selection : theme_.panelAlt));
    graphics.FillRectangle(&background, bounds);

    if (photoBitmap_ && photoBitmap_->GetLastStatus() == Gdiplus::Ok) {
        const float sourceWidth = static_cast<float>(photoBitmap_->GetWidth());
        const float sourceHeight = static_cast<float>(photoBitmap_->GetHeight());
        const float scale = std::max(width / sourceWidth, height / sourceHeight);
        const float cropWidth = width / scale;
        const float cropHeight = height / scale;
        graphics.DrawImage(photoBitmap_.get(), bounds, (sourceWidth - cropWidth) * 0.5f,
                           (sourceHeight - cropHeight) * 0.5f, cropWidth, cropHeight, Gdiplus::UnitPixel);
        const float actionHeight = std::max(34.0f, 42.0f * uiScale_);
        Gdiplus::SolidBrush overlay(Color(theme_.panel));
        graphics.FillRectangle(&overlay, 0.0f, height - actionHeight, width, actionHeight);
    } else {
        Gdiplus::SolidBrush silhouette(Color(theme_.textMuted));
        const float centerX = width * 0.5f;
        const float head = std::min(width, height) * 0.12f;
        graphics.FillEllipse(&silhouette, centerX - head, height * 0.25f - head, head * 2.0f, head * 2.0f);
        graphics.FillEllipse(&silhouette, centerX - head * 1.7f, height * 0.43f, head * 3.4f, head * 2.0f);
    }

    Gdiplus::Pen border(Color(focused ? theme_.accent : theme_.border), focused ? 2.0f : 1.0f);
    graphics.DrawRectangle(&border, 0.5f, 0.5f, std::max(0.0f, width - 1.0f), std::max(0.0f, height - 1.0f));
    Gdiplus::Font font(L"Segoe UI", 13.0f * uiScale_, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Color(theme_.text));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentFar);
    const std::wstring caption = WindowText(photo_);
    Gdiplus::RectF textBounds(8.0f, 8.0f, std::max(0.0f, width - 16.0f), std::max(0.0f, height - 16.0f));
    graphics.DrawString(caption.c_str(), -1, &font, textBounds, &format, &text);
    return true;
}

void CharacterEditorPanel::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    InvalidateRect(categories_, nullptr, TRUE);
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

}  // namespace mezozoy::ui
