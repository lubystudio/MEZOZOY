#include "LocationEditorPanel.h"

#include "Win32Util.h"
#include "../services/PosterService.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>

namespace mezozoy::ui {
namespace {

constexpr UINT_PTR WheelForwardSubclass = 0x4D5A4C4F;

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
    L"Главное", L"Драматургия", L"Пространство", L"Визуальный образ",
    L"Атмосфера", L"История и заметки"
};

constexpr const wchar_t* CategoryRows[] = {
    L"▣   Главное", L"◆   Драматургия", L"⌂   Пространство", L"◐   Визуальный образ",
    L"≈   Атмосфера", L"✎   История и заметки"
};

}  // namespace

LocationEditorPanel::LocationEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document) {
    Gdiplus::GdiplusStartupInput startup;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &startup, nullptr);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, GetWindowLongPtrW(hwnd_, GWL_STYLE) | WS_VSCROLL);

    back_ = CreateChild(L"BUTTON", L"‹  ЛОКАЦИЯ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdBack, hwnd_, instance_);
    categories_ = CreateChild(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP,
                              IdCategories, hwnd_, instance_, WS_EX_CLIENTEDGE);
    SetPropW(categories_,L"Mezozoy.Ui.List",reinterpret_cast<HANDLE>(1));
    for (const wchar_t* row : CategoryRows) SendMessageW(categories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
    sectionTitle_ = CreateChild(L"STATIC", L"ГЛАВНОЕ", SS_LEFT, 0, hwnd_, instance_);
    photo_ = CreateChild(L"BUTTON", L"+  ДОБАВИТЬ ФОТО", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdPhoto, hwnd_, instance_);
    locationCard_ = CreateChild(L"STATIC", L"ЛОКАЦИЯ", SS_LEFT, 0, hwnd_, instance_);
    for (HWND control : {back_, categories_, sectionTitle_, photo_, locationCard_})
        remember(control, control == sectionTitle_ || control == locationCard_);

    addField(0, L"name", L"Название");
    addField(0, L"type", L"Категория", false,
             {L"Не установлено", L"Город / район", L"Здание", L"Помещение", L"Природная зона",
              L"Транспорт", L"Виртуальное пространство", L"Другое"});
    addField(0, L"interiorExterior", L"Тип пространства", false,
             {L"Не установлено", L"Интерьер", L"Экстерьер", L"Смешанное"});
    addField(0, L"locationImportance", L"Значение в истории", false,
             {L"Не установлено", L"Ключевая", L"Повторяющаяся", L"Эпизодическая"});
    addField(0, L"timePeriod", L"Эпоха / исторический период");
    addField(0, L"shortDescription", L"Краткое описание");
    addField(0, L"description", L"Подробное описание", true);

    addField(1, L"storyFunction", L"Функция в сюжете", true);
    addField(1, L"scenePurpose", L"Какие сцены здесь происходят", true);
    addField(1, L"emotionalFunction", L"Эмоциональная функция");
    addField(1, L"conflictPotential", L"Источник конфликта", true);
    addField(1, L"firstAppearance", L"Первое появление");
    addField(1, L"keyEvents", L"Ключевые события", true);
    addField(1, L"changeOverStory", L"Как меняется по ходу истории", true);
    addField(1, L"symbolism", L"Символика и метафоры", true);
    addField(1, L"secrets", L"Тайны локации", true);
    addField(1, L"associatedCharacters", L"Связанные персонажи");
    addField(1, L"associatedObjects", L"Значимые предметы");

    addField(2, L"geography", L"Географическое положение");
    addField(2, L"surroundings", L"Окружение", true);
    addField(2, L"scale", L"Масштаб и размеры");
    addField(2, L"layout", L"Планировка", true);
    addField(2, L"zones", L"Функциональные зоны", true);
    addField(2, L"entrances", L"Входы");
    addField(2, L"exits", L"Выходы и пути отступления");
    addField(2, L"routes", L"Маршруты перемещения", true);
    addField(2, L"verticality", L"Уровни / этажи / высота");
    addField(2, L"visibility", L"Обзор и слепые зоны");
    addField(2, L"acoustics", L"Акустика пространства");
    addField(2, L"crowdCapacity", L"Вместимость");
    addField(2, L"accessibility", L"Доступность");
    addField(2, L"hiddenAreas", L"Скрытые помещения и проходы", true);

    addField(3, L"architecture", L"Архитектурный стиль");
    addField(3, L"materials", L"Материалы и фактуры");
    addField(3, L"palette", L"Цветовая палитра");
    addField(3, L"lighting", L"Общий характер освещения");
    addField(3, L"naturalLight", L"Естественный свет");
    addField(3, L"artificialLight", L"Искусственный свет");
    addField(3, L"visualMood", L"Визуальное настроение", true);
    addField(3, L"weather", L"Погода и климат");
    addField(3, L"season", L"Сезон");
    addField(3, L"condition", L"Состояние и степень износа");
    addField(3, L"distinctiveObjects", L"Визуальные доминанты", true);
    addField(3, L"signs", L"Надписи, знаки, графика");
    addField(3, L"floraFauna", L"Растения и животный мир");

    addField(4, L"sounds", L"Характерные звуки", true);
    addField(4, L"smells", L"Запахи");
    addField(4, L"temperature", L"Температура");
    addField(4, L"humidity", L"Влажность и воздух");
    addField(4, L"tactile", L"Тактильные ощущения");
    addField(4, L"crowd", L"Люди и плотность движения");
    addField(4, L"danger", L"Опасности", true);
    addField(4, L"comfort", L"Уровень комфорта");
    addField(4, L"rhythm", L"Ритм жизни места");
    addField(4, L"emotionalTone", L"Эмоциональный тон", true);

    addField(5, L"origin", L"Происхождение места", true);
    addField(5, L"pastEvents", L"Важные события прошлого", true);
    addField(5, L"currentUse", L"Текущее назначение");
    addField(5, L"localRules", L"Правила и запреты", true);
    addField(5, L"legends", L"Легенды и слухи", true);
    addField(5, L"continuity", L"Непрерывность между сценами", true);
    addField(5, L"props", L"Постоянный реквизит", true);
    addField(5, L"damageHistory", L"Повреждения и изменения", true);
    addField(5, L"transformation", L"Планируемая трансформация", true);
    addField(5, L"notes", L"Общие заметки", true);
    addField(5, L"research", L"Материалы исследования", true);
    addField(5, L"sourceLinks", L"Источники и ссылки", true);

    SendMessageW(categories_, LB_SETCURSEL, 0, 0);
    showCategory(0);
}

LocationEditorPanel::~LocationEditorPanel() {
    photoBitmap_.reset();
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

LocationEditorPanel::FieldControl& LocationEditorPanel::addField(
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
        const DWORD style = WS_TABSTOP | (multiline ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL);
        field.editor = CreateChild(L"EDIT", L"", style, field.id, hwnd_, instance_, WS_EX_CLIENTEDGE);
    }
    remember(field.labelWindow);
    remember(field.editor);
    forwardWheelToPage(field.editor);
    fields_.push_back(field);
    return fields_.back();
}

void LocationEditorPanel::forwardWheelToPage(HWND control) {
    SetWindowSubclass(control, ForwardMouseWheel, WheelForwardSubclass, reinterpret_cast<DWORD_PTR>(hwnd_));
}

Location* LocationEditorPanel::location() { return document_.locationById(locationId_); }
const Location* LocationEditorPanel::location() const { return document_.locationById(locationId_); }

std::wstring LocationEditorPanel::fieldValue(const Location& item, const std::wstring& key) const {
    if (key == L"name") return item.name;
    if (key == L"type") return item.type;
    if (key == L"description") return item.description;
    if (key == L"shortDescription") return item.shortDescription;
    if (key == L"interiorExterior") return item.interiorExterior;
    if (key == L"timePeriod") return item.timePeriod;
    if (key == L"visualMood") return item.visualMood;
    if (key == L"notes") return item.notes;
    const auto found = item.profile.find(key);
    return found == item.profile.end() ? std::wstring{} : found->second;
}

void LocationEditorPanel::setFieldValue(Location& item, const std::wstring& key, const std::wstring& value, bool& changed) {
    auto assign = [&](std::wstring& target) { if (target != value) { target = value; changed = true; } };
    if (key == L"name") assign(item.name);
    else if (key == L"type") assign(item.type);
    else if (key == L"description") assign(item.description);
    else if (key == L"shortDescription") assign(item.shortDescription);
    else if (key == L"interiorExterior") assign(item.interiorExterior);
    else if (key == L"timePeriod") assign(item.timePeriod);
    else if (key == L"visualMood") assign(item.visualMood);
    else if (key == L"notes") assign(item.notes);
    else {
        const auto found = item.profile.find(key);
        const std::wstring current = found == item.profile.end() ? std::wstring{} : found->second;
        if (current == value) return;
        if (value.empty()) item.profile.erase(key); else item.profile[key] = value;
        changed = true;
    }
}

void LocationEditorPanel::setLocation(int locationId) {
    ScopedRedrawLock redraw(hwnd_);
    if (locationId_ && locationId_ != locationId) commit();
    locationId_ = locationId;
    category_ = 0;
    scrollOffset_ = 0;
    loadFields();
    showCategory(0);
}

void LocationEditorPanel::loadFields() {
    loading_ = true;
    const Location* item = location();
    for (const auto& field : fields_) SetWindowTextW(field.editor, item ? fieldValue(*item, field.key).c_str() : L"");
    const std::wstring name = item && !item->name.empty() ? item->name : L"ЛОКАЦИЯ";
    SetWindowTextW(back_, (L"‹  " + name).c_str());
    updatePhotoCaption();
    loading_ = false;
}

void LocationEditorPanel::saveField(const FieldControl& field) {
    if (loading_) return;
    const std::wstring value = WindowText(field.editor);
    const bool updated = document_.updateLocation(locationId_, [&](Location& item) {
        bool changed = false;
        setFieldValue(item, field.key, value, changed);
        return changed;
    });
    if (!updated) return;
    if (field.key == L"name") {
        const Location* item = location();
        const std::wstring name = !item || item->name.empty() ? L"ЛОКАЦИЯ" : item->name;
        SetWindowTextW(back_, (L"‹  " + name).c_str());
        if (nameChangedAction_) nameChangedAction_();
    }
}

void LocationEditorPanel::commit() {
    if (loading_) return;
    for (const auto& field : fields_) saveField(field);
}

bool LocationEditorPanel::handleCommand(int id, int code, HWND) {
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
    if (id >= IdFirstField && id < IdFirstField + static_cast<int>(fields_.size())) {
        const auto& field = fields_[static_cast<std::size_t>(id - IdFirstField)];
        if ((!field.combo && code == EN_CHANGE) || (field.combo && (code == CBN_EDITCHANGE || code == CBN_SELCHANGE))) {
            saveField(field);
            return true;
        }
    }
    return false;
}

void LocationEditorPanel::showCategory(int category, bool resetScroll) {
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
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
}

void LocationEditorPanel::onLayout(int width, int height) {
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
    const int photoWidth = showPhoto ? scaled(260) : 0;
    const int photoGap = showPhoto ? scaled(26) : 0;
    const int fieldWidth = std::max(260, width - contentX - contentRight - photoWidth - photoGap);
    SetWindowPos(sectionTitle_, nullptr, contentX, scaled(18), fieldWidth, scaled(34), SWP_NOZORDER);

    if (showPhoto) {
        const int photoX = width - contentRight - photoWidth;
        ShowWindow(photo_, SW_SHOW);
        ShowWindow(locationCard_, SW_SHOW);
        SetWindowPos(photo_, nullptr, photoX, scaled(72), photoWidth, scaled(190), SWP_NOZORDER);
        SetWindowPos(locationCard_, nullptr, photoX + scaled(12), scaled(286), photoWidth - scaled(24), scaled(96), SWP_NOZORDER);
    } else {
        ShowWindow(photo_, SW_HIDE);
        ShowWindow(locationCard_, SW_HIDE);
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

    if (const Location* item = location()) {
        const std::wstring type = item->type.empty() ? L"Не установлено" : item->type;
        const std::wstring period = item->timePeriod.empty() ? L"Период не указан" : item->timePeriod;
        const std::wstring caption = (item->name.empty() ? L"ЛОКАЦИЯ" : item->name) + L"\r\nТип: " + type + L"\r\n" + period;
        SetWindowTextW(locationCard_, caption.c_str());
    }
}

void LocationEditorPanel::updateScrollBar() {
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

void LocationEditorPanel::setScrollOffset(int value) {
    const int maximum = std::max(0, contentHeight_ - viewportHeight_);
    const int next = std::clamp(value, 0, maximum);
    if (next == scrollOffset_) return;
    ScopedRedrawLock redraw(hwnd_);
    scrollOffset_ = next;
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
}

LRESULT LocationEditorPanel::onMessage(UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_VSCROLL) {
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(hwnd_, SB_VERT, &info);
        int next = scrollOffset_;
        switch (LOWORD(wParam)) {
            case SB_LINEUP: next -= 48; break;
            case SB_LINEDOWN: next += 48; break;
            case SB_PAGEUP: next -= viewportHeight_; break;
            case SB_PAGEDOWN: next += viewportHeight_; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: next = info.nTrackPos; break;
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

void LocationEditorPanel::choosePhoto() {
    const Location* item = location();
    if (!item) return;
    const auto path = OpenFileDialog(hwnd_, false,
        L"Изображения (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Все файлы (*.*)\0*.*\0\0", L"png");
    if (path.empty()) return;

    Location imported = *item;
    std::wstring error;
    if (!PosterService::importFile(path, imported, &error)) {
        MessageBoxW(hwnd_, error.c_str(), L"Фотография локации", MB_OK | MB_ICONERROR);
        return;
    }
    const auto importedData = imported.imageData;
    const std::wstring importedFormat = imported.imageFormat;
    const std::wstring importedName = imported.imagePath;
    document_.updateLocation(locationId_, [&](Location& target) {
        const std::wstring oldData = target.imageData ? *target.imageData : std::wstring{};
        const std::wstring newData = importedData ? *importedData : std::wstring{};
        if (oldData == newData && target.imageFormat == importedFormat && target.imagePath == importedName) return false;
        target.imageData = importedData;
        target.imageFormat = importedFormat;
        target.imagePath = importedName;
        return true;
    }, L"Изменение фотографии локации");
    updatePhotoCaption();
}

void LocationEditorPanel::updatePhotoCaption() {
    const Location* item = location();
    const bool hasPhoto = item && (item->imageData || !item->imagePath.empty());
    SetWindowTextW(photo_, hasPhoto ? L"✎  ИЗМЕНИТЬ ФОТО" : L"+  ДОБАВИТЬ ФОТО");
    refreshPhotoBitmap();
    InvalidateRect(photo_, nullptr, TRUE);
}

void LocationEditorPanel::refreshPhotoBitmap() {
    photoBitmap_.reset();
    const Location* item = location();
    if (!item || !gdiplusToken_) return;
    photoBitmap_ = BitmapFromBytes(PosterService::decode(*item));
    if (photoBitmap_ || item->imagePath.empty()) return;

    std::error_code error;
    const std::filesystem::path legacyPath(item->imagePath);
    if (!std::filesystem::is_regular_file(legacyPath, error)) return;
    Gdiplus::Bitmap source(legacyPath.c_str());
    photoBitmap_ = CloneBitmap(&source);
}

bool LocationEditorPanel::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON || draw->hwndItem != photo_) return false;
    const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
    const bool focused = (draw->itemState & ODS_FOCUS) != 0;
    Gdiplus::Graphics graphics(draw->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    const float width = static_cast<float>(draw->rcItem.right - draw->rcItem.left);
    const float height = static_cast<float>(draw->rcItem.bottom - draw->rcItem.top);
    const Gdiplus::RectF bounds(0.0f, 0.0f, width, height);
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
        Gdiplus::SolidBrush placeholder(Color(theme_.textMuted));
        Gdiplus::Pen placeholderPen(Color(theme_.textMuted), 3.0f);
        graphics.FillEllipse(&placeholder, width * 0.68f, height * 0.22f, height * 0.12f, height * 0.12f);
        Gdiplus::PointF mountain[] = {
            {width * 0.18f, height * 0.66f}, {width * 0.42f, height * 0.38f},
            {width * 0.57f, height * 0.56f}, {width * 0.70f, height * 0.44f},
            {width * 0.84f, height * 0.66f}
        };
        graphics.DrawLines(&placeholderPen, mountain, static_cast<INT>(std::size(mountain)));
    }

    Gdiplus::Pen border(Color(focused ? theme_.accent : theme_.border), focused ? 2.0f : 1.0f);
    graphics.DrawRectangle(&border, 0.5f, 0.5f, std::max(0.0f, width - 1.0f), std::max(0.0f, height - 1.0f));
    Gdiplus::Font font(L"Segoe UI", 13.0f * uiScale_, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Color(theme_.text));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentFar);
    const std::wstring caption = WindowText(photo_);
    const Gdiplus::RectF textBounds(8.0f, 8.0f, std::max(0.0f, width - 16.0f), std::max(0.0f, height - 16.0f));
    graphics.DrawString(caption.c_str(), -1, &font, textBounds, &format, &text);
    return true;
}

void LocationEditorPanel::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    InvalidateRect(categories_, nullptr, TRUE);
    InvalidateRect(photo_, nullptr, TRUE);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
}

}  // namespace mezozoy::ui
