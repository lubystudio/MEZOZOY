#include "ReferenceEditorPanel.h"

#include "Dialogs.h"
#include "Win32Util.h"
#include "../services/PosterService.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace mezozoy::ui {
namespace {

Gdiplus::Color Color(COLORREF value, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(value), GetGValue(value), GetBValue(value));
}

std::unique_ptr<Gdiplus::Bitmap> CloneBitmap(Gdiplus::Bitmap* source) {
    if (!source || source->GetLastStatus() != Gdiplus::Ok || source->GetWidth() == 0 || source->GetHeight() == 0) return {};
    constexpr float maximumPreviewSide = 1400.0f;
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

bool StartsWithInsensitive(const std::wstring& value, const std::wstring& prefix) {
    if (prefix.size() > value.size()) return false;
    for (std::size_t index = 0; index < prefix.size(); ++index)
        if (std::towlower(value[index]) != std::towlower(prefix[index])) return false;
    return true;
}

std::wstring ComboText(HWND combo, int index) {
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
    if (length < 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<std::size_t>(length));
    return text;
}

}  // namespace

ReferenceEditorPanel::ReferenceEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document) {
    Gdiplus::GdiplusStartupInput startup;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &startup, nullptr);

    heading_ = CreateChild(L"STATIC", L"РЕФЕРЕНС", SS_LEFT, 0, hwnd_, instance_);
    titleLabel_ = CreateChild(L"STATIC", L"▧  Название", SS_LEFT, 0, hwnd_, instance_);
    title_ = CreateChild(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IdTitle, hwnd_, instance_, WS_EX_CLIENTEDGE);
    aspectLabel_ = CreateChild(L"STATIC", L"▣  Формат изображения", SS_LEFT, 0, hwnd_, instance_);
    aspect169_ = CreateChild(L"BUTTON", L"16:9", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdAspect169, hwnd_, instance_);
    aspect916_ = CreateChild(L"BUTTON", L"9:16", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdAspect916, hwnd_, instance_);
    aspect11_ = CreateChild(L"BUTTON", L"1:1", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdAspect11, hwnd_, instance_);
    tagLabel_ = CreateChild(L"STATIC", L"⌕  Связать с персонажем или локацией", SS_LEFT, 0, hwnd_, instance_);
    tag_ = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
                       IdTag, hwnd_, instance_, WS_EX_CLIENTEDGE);
    tagHint_ = CreateChild(L"STATIC", L"Начните вводить имя, затем выберите подсказку", SS_LEFT | SS_ENDELLIPSIS,
                           0, hwnd_, instance_);
    image_ = CreateChild(L"BUTTON", L"+  ДОБАВИТЬ ИЗОБРАЖЕНИЕ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP,
                         IdImage, hwnd_, instance_);
    imageStatus_ = CreateChild(L"STATIC", L"Изображение не выбрано", SS_CENTER | SS_ENDELLIPSIS,
                               0, hwnd_, instance_);
    imageRemove_ = CreateChild(L"BUTTON", L"Удалить изображение", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP,
                               IdImageRemove, hwnd_, instance_);
    for (HWND control : {heading_, titleLabel_, title_, aspectLabel_, aspect169_, aspect916_, aspect11_, tagLabel_, tag_,
                         tagHint_, image_, imageStatus_, imageRemove_})
        remember(control, control == heading_);
    ShowWindow(imageRemove_, SW_HIDE);
}

ReferenceEditorPanel::~ReferenceEditorPanel() {
    imageBitmap_.reset();
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

ReferenceItem* ReferenceEditorPanel::reference() {
    return document_.referenceById(referenceId_);
}

const ReferenceItem* ReferenceEditorPanel::reference() const {
    return document_.referenceById(referenceId_);
}

void ReferenceEditorPanel::setReference(int referenceId) {
    ScopedRedrawLock redraw(hwnd_);
    if (referenceId_ && referenceId_ != referenceId) commit();
    referenceId_ = referenceId;
    refresh();
}

void ReferenceEditorPanel::refresh() {
    loading_ = true;
    const ReferenceItem* item = reference();
    SetWindowTextW(title_, item ? item->title.c_str() : L"");
    rebuildTagOptions();
    selectCurrentTag();
    updateImage();
    loading_ = false;
    for (HWND button : {aspect169_, aspect916_, aspect11_}) InvalidateRect(button, nullptr, TRUE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void ReferenceEditorPanel::saveTitle() {
    if (loading_ || !referenceId_) return;
    const std::wstring value = WindowText(title_);
    const bool changed = document_.updateReference(referenceId_, [&](ReferenceItem& item) {
        if (item.title == value) return false;
        item.title = value;
        return true;
    });
    if (changed && nameChangedAction_) nameChangedAction_();
}

void ReferenceEditorPanel::commit() {
    saveTitle();
    const int exact = findTagByText(WindowText(tag_), false);
    if (exact >= 0) applyTagSelection(exact);
}

void ReferenceEditorPanel::setAspect(const wchar_t* aspect) {
    if (!referenceId_ || !aspect) return;
    document_.updateReference(referenceId_, [&](ReferenceItem& item) {
        if (item.aspectRatio == aspect) return false;
        item.aspectRatio = aspect;
        return true;
    }, L"Изменение формата референса");
    for (HWND button : {aspect169_, aspect916_, aspect11_}) InvalidateRect(button, nullptr, TRUE);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
}

void ReferenceEditorPanel::rebuildTagOptions() {
    SendMessageW(tag_, CB_RESETCONTENT, 0, 0);
    int index = static_cast<int>(SendMessageW(tag_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Без связи")));
    SendMessageW(tag_, CB_SETITEMDATA, index, 0);
    for (const auto& item : document_.project().characters) {
        const std::wstring caption = L"Персонаж · " + item.name;
        index = static_cast<int>(SendMessageW(tag_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str())));
        SendMessageW(tag_, CB_SETITEMDATA, index, item.id);
    }
    for (const auto& item : document_.project().locations) {
        const std::wstring caption = L"Локация · " + item.name;
        index = static_cast<int>(SendMessageW(tag_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str())));
        SendMessageW(tag_, CB_SETITEMDATA, index, -item.id);
    }
}

void ReferenceEditorPanel::selectCurrentTag() {
    const ReferenceItem* item = reference();
    if (!item) return;
    const LPARAM expected = item->linkedCharacterId ? item->linkedCharacterId
                             : item->linkedLocationId ? -item->linkedLocationId : 0;
    const int count = static_cast<int>(SendMessageW(tag_, CB_GETCOUNT, 0, 0));
    int selected = 0;
    for (int index = 0; index < count; ++index) {
        if (SendMessageW(tag_, CB_GETITEMDATA, index, 0) == expected) {
            selected = index;
            break;
        }
    }
    SendMessageW(tag_, CB_SETCURSEL, selected, 0);
}

int ReferenceEditorPanel::findTagByText(const std::wstring& text, bool prefix) const {
    if (text.empty()) return 0;
    const int count = static_cast<int>(SendMessageW(tag_, CB_GETCOUNT, 0, 0));
    for (int index = 0; index < count; ++index) {
        const std::wstring caption = ComboText(tag_, index);
        const std::size_t separator = caption.find(L" · ");
        const std::wstring name = separator == std::wstring::npos ? caption : caption.substr(separator + 3);
        if (prefix ? (StartsWithInsensitive(name, text) || StartsWithInsensitive(caption, text))
                   : (_wcsicmp(caption.c_str(), text.c_str()) == 0 || _wcsicmp(name.c_str(), text.c_str()) == 0))
            return index;
    }
    return -1;
}

void ReferenceEditorPanel::autocompleteTag() {
    if (loading_ || autoCompleting_) return;
    const std::wstring typed = WindowText(tag_);
    if (typed.empty()) return;
    const int match = findTagByText(typed, true);
    if (match < 0) return;
    autoCompleting_ = true;
    SendMessageW(tag_, CB_SHOWDROPDOWN, TRUE, 0);
    SendMessageW(tag_, CB_SETCURSEL, match, 0);
    SendMessageW(tag_, CB_SETEDITSEL, 0, MAKELPARAM(typed.size(), -1));
    autoCompleting_ = false;
}

void ReferenceEditorPanel::applyTagSelection(int index) {
    if (loading_ || autoCompleting_ || !referenceId_ || index < 0) return;
    const LPARAM data = SendMessageW(tag_, CB_GETITEMDATA, index, 0);
    const int characterId = data > 0 ? static_cast<int>(data) : 0;
    const int locationId = data < 0 ? static_cast<int>(-data) : 0;
    const std::wstring caption = data ? ComboText(tag_, index) : std::wstring{};
    document_.updateReference(referenceId_, [&](ReferenceItem& item) {
        if (item.linkedCharacterId == characterId && item.linkedLocationId == locationId && item.tags == caption) return false;
        item.linkedCharacterId = characterId;
        item.linkedLocationId = locationId;
        item.tags = caption;
        return true;
    }, L"Связь референса");
}

bool ReferenceEditorPanel::handleCommand(int id, int code, HWND) {
    if (id == IdTitle && code == EN_CHANGE) { saveTitle(); return true; }
    if (id == IdAspect169 && code == BN_CLICKED) { setAspect(L"16:9"); return true; }
    if (id == IdAspect916 && code == BN_CLICKED) { setAspect(L"9:16"); return true; }
    if (id == IdAspect11 && code == BN_CLICKED) { setAspect(L"1:1"); return true; }
    if (id == IdImage && code == BN_CLICKED) { chooseImage(); return true; }
    if (id == IdImageRemove && code == BN_CLICKED) { removeImage(); return true; }
    if (id == IdTag) {
        if (code == CBN_EDITCHANGE) { autocompleteTag(); return true; }
        if (code == CBN_SELCHANGE || code == CBN_SELENDOK) {
            applyTagSelection(static_cast<int>(SendMessageW(tag_, CB_GETCURSEL, 0, 0)));
            return true;
        }
        if (code == CBN_KILLFOCUS) {
            const int exact = findTagByText(WindowText(tag_), false);
            if (exact >= 0) {
                SendMessageW(tag_, CB_SETCURSEL, exact, 0);
                applyTagSelection(exact);
            } else {
                loading_ = true;
                selectCurrentTag();
                loading_ = false;
            }
            return true;
        }
    }
    return false;
}

void ReferenceEditorPanel::chooseImage() {
    const ReferenceItem* item = reference();
    if (!item) return;
    const auto path = OpenFileDialog(hwnd_, false,
        L"Изображения (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Все файлы (*.*)\0*.*\0\0", L"png");
    if (path.empty()) return;
    ReferenceItem imported = *item;
    std::wstring error;
    if (!PosterService::importFile(path, imported, &error)) {
        MessageBoxW(hwnd_, error.c_str(), L"Изображение референса", MB_OK | MB_ICONERROR);
        return;
    }
    const auto data = imported.imageData;
    const std::wstring format = imported.imageFormat;
    const std::wstring name = imported.imagePath;
    document_.updateReference(referenceId_, [&](ReferenceItem& target) {
        const std::wstring before = target.imageData ? *target.imageData : std::wstring{};
        const std::wstring after = data ? *data : std::wstring{};
        if (before == after && target.imageFormat == format && target.imagePath == name) return false;
        target.imageData = data;
        target.imageFormat = format;
        target.imagePath = name;
        return true;
    }, L"Изменение изображения референса");
    updateImage();
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void ReferenceEditorPanel::removeImage() {
    if (!reference() || !reference()->imageData) return;
    if (MessageBoxW(hwnd_, L"Удалить изображение из референса?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    document_.updateReference(referenceId_, [](ReferenceItem& item) {
        if (!item.imageData && item.imagePath.empty()) return false;
        PosterService::clear(item);
        return true;
    }, L"Удаление изображения референса");
    updateImage();
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void ReferenceEditorPanel::updateImage() {
    const ReferenceItem* item = reference();
    const bool hasImage = item && (item->imageData || !item->imagePath.empty());
    SetWindowTextW(image_, hasImage ? L"✎  ЗАМЕНИТЬ ИЗОБРАЖЕНИЕ" : L"+  ДОБАВИТЬ ИЗОБРАЖЕНИЕ");
    const std::wstring status = item && item->imageData
        ? L"Встроено в проект · " + item->imageFormat
        : hasImage ? L"Старый внешний файл" : L"Изображение не выбрано";
    SetWindowTextW(imageStatus_, status.c_str());
    ShowWindow(imageRemove_, hasImage ? SW_SHOW : SW_HIDE);
    refreshImageBitmap();
    InvalidateRect(image_, nullptr, TRUE);
}

void ReferenceEditorPanel::refreshImageBitmap() {
    imageBitmap_.reset();
    const ReferenceItem* item = reference();
    if (!item || !gdiplusToken_) return;
    imageBitmap_ = BitmapFromBytes(PosterService::decode(*item));
    if (imageBitmap_ || item->imagePath.empty()) return;
    std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromFile(item->imagePath.c_str(), FALSE));
    imageBitmap_ = CloneBitmap(source.get());
}

void ReferenceEditorPanel::onLayout(int width, int height) {
    const auto scaled = [this](int value) {
        return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * uiScale_)));
    };
    const int padding = scaled(30);
    const int gap = scaled(34);
    const bool sideBySide = width >= scaled(760);
    const int metadataWidth = sideBySide ? std::clamp(width / 3, scaled(300), scaled(410))
                                         : std::max(scaled(280), width - padding * 2);
    SetWindowPos(heading_, nullptr, padding, scaled(18), metadataWidth, scaled(34), SWP_NOZORDER | SWP_NOACTIVATE);

    const int labelHeight = scaled(22);
    SetWindowPos(titleLabel_, nullptr, padding, scaled(70), metadataWidth, labelHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(title_, nullptr, padding, scaled(96), metadataWidth, scaled(38), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(aspectLabel_, nullptr, padding, scaled(162), metadataWidth, labelHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    const int aspectGap = scaled(8);
    const int aspectWidth = std::max(scaled(70), (metadataWidth - aspectGap * 2) / 3);
    SetWindowPos(aspect169_, nullptr, padding, scaled(190), aspectWidth, scaled(40), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(aspect916_, nullptr, padding + aspectWidth + aspectGap, scaled(190), aspectWidth, scaled(40), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(aspect11_, nullptr, padding + (aspectWidth + aspectGap) * 2, scaled(190), aspectWidth, scaled(40), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(tagLabel_, nullptr, padding, scaled(260), metadataWidth, labelHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(tag_, nullptr, padding, scaled(288), metadataWidth, scaled(240), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(tagHint_, nullptr, padding, scaled(330), metadataWidth, scaled(24), SWP_NOZORDER | SWP_NOACTIVATE);

    const int previewX = sideBySide ? padding + metadataWidth + gap : padding;
    const int previewY = sideBySide ? scaled(70) : scaled(382);
    const int previewWidth = sideBySide ? std::max(scaled(220), width - previewX - padding) : metadataWidth;
    const int previewHeight = std::max(scaled(180), height - previewY - scaled(92));
    const std::wstring aspect = reference() && !reference()->aspectRatio.empty() ? reference()->aspectRatio : L"16:9";
    float ratio = 16.0f / 9.0f;
    if (aspect == L"9:16") ratio = 9.0f / 16.0f;
    else if (aspect == L"1:1") ratio = 1.0f;
    int frameWidth = previewWidth;
    int frameHeight = static_cast<int>(std::lround(frameWidth / ratio));
    if (frameHeight > previewHeight) {
        frameHeight = previewHeight;
        frameWidth = static_cast<int>(std::lround(frameHeight * ratio));
    }
    frameWidth = std::max(scaled(120), frameWidth);
    frameHeight = std::max(scaled(120), frameHeight);
    const int frameX = previewX + std::max(0, (previewWidth - frameWidth) / 2);
    SetWindowPos(image_, nullptr, frameX, previewY, frameWidth, frameHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(imageStatus_, nullptr, previewX, previewY + frameHeight + scaled(8), previewWidth, scaled(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(imageRemove_, nullptr, previewX + std::max(0, (previewWidth - scaled(190)) / 2),
                 previewY + frameHeight + scaled(38), scaled(190), scaled(38), SWP_NOZORDER | SWP_NOACTIVATE);
}

bool ReferenceEditorPanel::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON) return false;
    if (draw->hwndItem == aspect169_ || draw->hwndItem == aspect916_ || draw->hwndItem == aspect11_) {
        const wchar_t* value = draw->hwndItem == aspect169_ ? L"16:9" : draw->hwndItem == aspect916_ ? L"9:16" : L"1:1";
        const ReferenceItem* item = reference();
        const bool selected = item && (item->aspectRatio.empty() ? std::wstring(L"16:9") : item->aspectRatio) == value;
        const COLORREF fill = selected ? theme_.selection : theme_.panelAlt;
        HBRUSH background = CreateSolidBrush(fill);
        FillRect(draw->hDC, &draw->rcItem, background);
        DeleteObject(background);
        HBRUSH border = CreateSolidBrush(selected ? theme_.accent : theme_.border);
        FrameRect(draw->hDC, &draw->rcItem, border);
        DeleteObject(border);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, selected ? theme_.accent : theme_.text);
        SelectObject(draw->hDC, reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0)));
        RECT textRect = draw->rcItem;
        DrawTextW(draw->hDC, value, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return true;
    }
    if (draw->hwndItem != image_) return false;

    Gdiplus::Graphics graphics(draw->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    const float width = static_cast<float>(draw->rcItem.right - draw->rcItem.left);
    const float height = static_cast<float>(draw->rcItem.bottom - draw->rcItem.top);
    Gdiplus::SolidBrush background(Color(theme_.panelAlt));
    graphics.FillRectangle(&background, 0.0f, 0.0f, width, height);
    if (imageBitmap_ && imageBitmap_->GetLastStatus() == Gdiplus::Ok) {
        const float sourceWidth = static_cast<float>(imageBitmap_->GetWidth());
        const float sourceHeight = static_cast<float>(imageBitmap_->GetHeight());
        const float scale = std::min(width / sourceWidth, height / sourceHeight);
        const float imageWidth = sourceWidth * scale;
        const float imageHeight = sourceHeight * scale;
        const Gdiplus::RectF imageBounds((width - imageWidth) * 0.5f, (height - imageHeight) * 0.5f,
                                         imageWidth, imageHeight);
        graphics.DrawImage(imageBitmap_.get(), imageBounds, 0.0f, 0.0f, sourceWidth, sourceHeight, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush overlay(Color(theme_.panel));
        graphics.FillRectangle(&overlay, 0.0f, height - 46.0f * uiScale_, width, 46.0f * uiScale_);
    }
    Gdiplus::Pen border(Color((draw->itemState & ODS_FOCUS) ? theme_.accent : theme_.border),
                        (draw->itemState & ODS_FOCUS) ? 2.0f : 1.0f);
    graphics.DrawRectangle(&border, 0.5f, 0.5f, std::max(0.0f, width - 1.0f), std::max(0.0f, height - 1.0f));
    Gdiplus::Font font(L"Segoe UI", 13.0f * uiScale_, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Color(theme_.text));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(imageBitmap_ ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentCenter);
    const std::wstring caption = WindowText(image_);
    const Gdiplus::RectF textBounds(8.0f, 8.0f, std::max(0.0f, width - 16.0f), std::max(0.0f, height - 16.0f));
    graphics.DrawString(caption.c_str(), -1, &font, textBounds, &format, &text);
    return true;
}

void ReferenceEditorPanel::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
    for (HWND control : {aspect169_, aspect916_, aspect11_, image_}) InvalidateRect(control, nullptr, TRUE);
}

}  // namespace mezozoy::ui
