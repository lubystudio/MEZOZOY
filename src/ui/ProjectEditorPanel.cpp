#include "ProjectEditorPanel.h"

#include "Dialogs.h"
#include "Win32Util.h"
#include "../services/PosterService.h"

#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace mezozoy::ui {
namespace {

constexpr UINT_PTR WheelForwardSubclass = 0x4D5A5052;

LRESULT CALLBACK ForwardMouseWheel(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR subclassId, DWORD_PTR reference) {
    if (message == WM_MOUSEWHEEL) {
        SendMessageW(reinterpret_cast<HWND>(reference), message, wParam, lParam);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ForwardMouseWheel, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

Gdiplus::Color Color(COLORREF value, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(value), GetGValue(value), GetBValue(value));
}

std::unique_ptr<Gdiplus::Bitmap> CloneBitmap(Gdiplus::Bitmap* source) {
    if (!source || source->GetLastStatus() != Gdiplus::Ok || source->GetWidth() == 0 || source->GetHeight() == 0) return {};
    constexpr float maximumPreviewSide = 1200.0f;
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

}  // namespace

ProjectEditorPanel::ProjectEditorPanel(HINSTANCE instance, HWND parent, ProjectDocument& document, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document) {
    Gdiplus::GdiplusStartupInput startup;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &startup, nullptr);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, GetWindowLongPtrW(hwnd_, GWL_STYLE) | WS_VSCROLL);

    heading_ = CreateChild(L"STATIC", L"ПРОЕКТ", SS_LEFT, 0, hwnd_, instance_);
    poster_ = CreateChild(L"BUTTON", L"+  ДОБАВИТЬ АФИШУ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP,
                          IdPoster, hwnd_, instance_);
    posterStatus_ = CreateChild(L"STATIC", L"Афиша не выбрана", SS_CENTER | SS_ENDELLIPSIS, 0, hwnd_, instance_);
    posterRemove_ = CreateChild(L"BUTTON", L"Удалить афишу", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP,
                                IdPosterRemove, hwnd_, instance_);
    remember(heading_, true);
    remember(poster_);
    remember(posterStatus_);
    remember(posterRemove_);

    addField(L"title", L"▤  Название сценария", 0);
    addField(L"author", L"✎  Автор", 1, 0);
    addField(L"year", L"◷  Год", 1, 1);
    addField(L"subtitle", L"T  Титул / подзаголовок", 2);
    addField(L"tagline", L"◆  Слоган", 3);
    addField(L"genre", L"◇  Жанр", 4, 0, false,
             {L"Не указан", L"Драма", L"Комедия", L"Триллер", L"Детектив", L"Ужасы",
              L"Фантастика", L"Фэнтези", L"Мелодрама", L"Приключения", L"Документальный"});
    addField(L"projectType", L"▦  Формат", 4, 1, false,
             {L"Не указан", L"Полнометражный фильм", L"Короткометражный фильм", L"Сериал",
              L"Мини-сериал", L"Анимация", L"Документальный проект"});
    addField(L"draftLabel", L"●  Версия / черновик", 5, 0);
    addField(L"contact", L"@  Контакты автора", 5, 1);
    addField(L"copyright", L"©  Авторские права", 6);
    addField(L"logline", L"→  Логлайн", 7, -1, true);
    addField(L"description", L"≡  Описание проекта", 8, -1, true);

    refresh();
}

ProjectEditorPanel::~ProjectEditorPanel() {
    posterBitmap_.reset();
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

ProjectEditorPanel::FieldControl& ProjectEditorPanel::addField(
    const wchar_t* key, const wchar_t* label, int row, int column, bool multiline,
    std::initializer_list<const wchar_t*> options) {
    FieldControl field;
    field.id = IdFirstField + static_cast<int>(fields_.size());
    field.key = key;
    field.row = row;
    field.column = column;
    field.multiline = multiline;
    field.combo = options.size() > 0;
    field.label = CreateChild(L"STATIC", label, SS_LEFT | SS_ENDELLIPSIS, 0, hwnd_, instance_);
    if (field.combo) {
        field.editor = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
                                   field.id, hwnd_, instance_, WS_EX_CLIENTEDGE);
        for (const wchar_t* option : options) SendMessageW(field.editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option));
    } else {
        const DWORD style = WS_TABSTOP | (multiline
            ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN
            : ES_AUTOHSCROLL);
        field.editor = CreateChild(L"EDIT", L"", style, field.id, hwnd_, instance_, WS_EX_CLIENTEDGE);
    }
    remember(field.label, true);
    remember(field.editor);
    forwardWheelToPage(field.editor);
    fields_.push_back(field);
    return fields_.back();
}

std::wstring ProjectEditorPanel::fieldValue(const Project& project, const std::wstring& key) const {
    if (key == L"title") return project.title;
    if (key == L"author") return project.author;
    if (key == L"year") return project.year;
    if (key == L"subtitle") return project.subtitle;
    if (key == L"tagline") return project.tagline;
    if (key == L"genre") return project.genre;
    if (key == L"projectType") return project.projectType;
    if (key == L"draftLabel") return project.draftLabel;
    if (key == L"contact") return project.contact;
    if (key == L"copyright") return project.copyright;
    if (key == L"logline") return project.logline;
    if (key == L"description") return project.description;
    return {};
}

void ProjectEditorPanel::setFieldValue(Project& project, const std::wstring& key,
                                       const std::wstring& value, bool& changed) {
    auto assign = [&](std::wstring& target) {
        if (target != value) {
            target = value;
            changed = true;
        }
    };
    if (key == L"title") assign(project.title);
    else if (key == L"author") assign(project.author);
    else if (key == L"year") assign(project.year);
    else if (key == L"subtitle") assign(project.subtitle);
    else if (key == L"tagline") assign(project.tagline);
    else if (key == L"genre") assign(project.genre);
    else if (key == L"projectType") assign(project.projectType);
    else if (key == L"draftLabel") assign(project.draftLabel);
    else if (key == L"contact") assign(project.contact);
    else if (key == L"copyright") assign(project.copyright);
    else if (key == L"logline") assign(project.logline);
    else if (key == L"description") assign(project.description);
}

void ProjectEditorPanel::refresh() {
    ScopedRedrawLock redraw(hwnd_);
    loading_ = true;
    const Project& project = document_.project();
    for (const auto& field : fields_) SetWindowTextW(field.editor, fieldValue(project, field.key).c_str());
    updatePoster();
    loading_ = false;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void ProjectEditorPanel::saveField(const FieldControl& field) {
    if (loading_) return;
    const std::wstring value = WindowText(field.editor);
    const bool updated = document_.updateProject([&](Project& project) {
        bool changed = false;
        setFieldValue(project, field.key, value, changed);
        return changed;
    });
    if (updated && field.key == L"title" && nameChangedAction_) nameChangedAction_();
}

void ProjectEditorPanel::commit() {
    if (loading_) return;
    for (const auto& field : fields_) saveField(field);
}

bool ProjectEditorPanel::handleCommand(int id, int code, HWND) {
    if (id == IdPoster && code == BN_CLICKED) { choosePoster(); return true; }
    if (id == IdPosterRemove && code == BN_CLICKED) { removePoster(); return true; }
    if (id >= IdFirstField && id < IdFirstField + static_cast<int>(fields_.size())) {
        const auto& field = fields_[static_cast<std::size_t>(id - IdFirstField)];
        if ((!field.combo && code == EN_CHANGE) ||
            (field.combo && (code == CBN_EDITCHANGE || code == CBN_SELCHANGE))) {
            saveField(field);
            return true;
        }
    }
    return false;
}

void ProjectEditorPanel::onLayout(int width, int height) {
    const auto scaled = [this](int value) {
        return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * uiScale_)));
    };
    const int padding = scaled(30);
    const int gap = scaled(26);
    const bool showPoster = width >= scaled(700);
    const int posterWidth = showPoster ? std::clamp(width / 4, scaled(210), scaled(300)) : 0;
    const int posterX = width - padding - posterWidth;
    const int availableFormWidth = showPoster ? posterX - gap - padding : width - padding * 2;
    const int formWidth = std::max(scaled(320), std::min(scaled(740), availableFormWidth));
    const int columnGap = scaled(16);
    const int columnWidth = std::max(scaled(145), (formWidth - columnGap) / 2);

    SetWindowPos(heading_, nullptr, padding, scaled(18), formWidth, scaled(34), SWP_NOZORDER | SWP_NOACTIVATE);

    if (showPoster) {
        const int posterHeight = posterWidth * 3 / 2;
        ShowWindow(poster_, SW_SHOW);
        ShowWindow(posterStatus_, SW_SHOW);
        ShowWindow(posterRemove_, document_.project().posterImageData ? SW_SHOW : SW_HIDE);
        SetWindowPos(poster_, nullptr, posterX, scaled(68), posterWidth, posterHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(posterStatus_, nullptr, posterX, scaled(78) + posterHeight, posterWidth, scaled(24),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(posterRemove_, nullptr, posterX, scaled(110) + posterHeight, posterWidth, scaled(36),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        ShowWindow(poster_, SW_HIDE);
        ShowWindow(posterStatus_, SW_HIDE);
        ShowWindow(posterRemove_, SW_HIDE);
    }

    const int labelHeight = scaled(20);
    const int labelGap = scaled(4);
    const int rowGap = scaled(12);
    const int normalHeight = scaled(34);
    const std::array<int, 9> editHeights{
        normalHeight, normalHeight, normalHeight, normalHeight, normalHeight,
        normalHeight, normalHeight, scaled(68), scaled(88)
    };
    std::array<int, 9> rowTops{};
    int rowY = scaled(66);
    for (std::size_t row = 0; row < rowTops.size(); ++row) {
        rowTops[row] = rowY;
        rowY += labelHeight + labelGap + editHeights[row] + rowGap;
    }
    const int topClip = scaled(56);
    contentHeight_ = rowY + scaled(20);
    viewportHeight_ = std::max(1, height - topClip);
    scrollOffset_ = std::clamp(scrollOffset_, 0, std::max(0, contentHeight_ - viewportHeight_));
    updateScrollBar();

    constexpr UINT layoutFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;
    for (const auto& field : fields_) {
        const int row = std::clamp(field.row, 0, static_cast<int>(rowTops.size()) - 1);
        const bool paired = field.column >= 0;
        const int x = padding + (field.column == 1 ? columnWidth + columnGap : 0);
        const int fieldWidth = paired ? columnWidth : formWidth;
        const int y = rowTops[static_cast<std::size_t>(row)] - scrollOffset_;
        const int visualHeight = editHeights[static_cast<std::size_t>(row)];
        const int editorHeight = field.combo ? std::max(scaled(220), visualHeight) : visualHeight;
        const bool visible = y >= topClip && y < height && y + labelHeight + labelGap < height;
        const UINT visibility = visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
        SetWindowPos(field.label, nullptr, x, y, fieldWidth, labelHeight, layoutFlags | visibility);
        SetWindowPos(field.editor, nullptr, x, y + labelHeight + labelGap, fieldWidth, editorHeight,
                     layoutFlags | visibility);
    }
}

void ProjectEditorPanel::updateScrollBar() {
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, contentHeight_ - 1);
    info.nPage = static_cast<UINT>(viewportHeight_);
    info.nPos = scrollOffset_;
    SetScrollInfo(hwnd_, SB_VERT, &info, TRUE);
    ShowScrollBar(hwnd_, SB_VERT, contentHeight_ > viewportHeight_ ? TRUE : FALSE);
}

void ProjectEditorPanel::setScrollOffset(int value) {
    const int maximum = std::max(0, contentHeight_ - viewportHeight_);
    const int next = std::clamp(value, 0, maximum);
    if (next == scrollOffset_) return;
    ScopedRedrawLock redraw(hwnd_);
    scrollOffset_ = next;
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void ProjectEditorPanel::forwardWheelToPage(HWND control) {
    SetWindowSubclass(control, ForwardMouseWheel, WheelForwardSubclass, reinterpret_cast<DWORD_PTR>(hwnd_));
}

LRESULT ProjectEditorPanel::onMessage(UINT message, WPARAM wParam, LPARAM) {
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

void ProjectEditorPanel::choosePoster() {
    const auto path = OpenFileDialog(hwnd_, false,
        L"Изображения (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Все файлы (*.*)\0*.*\0\0", L"png");
    if (path.empty()) return;
    Project imported = document_.project();
    std::wstring error;
    if (!PosterService::importFile(path, imported, &error)) {
        MessageBoxW(hwnd_, error.c_str(), L"Афиша проекта", MB_OK | MB_ICONERROR);
        return;
    }
    const auto data = imported.posterImageData;
    const std::wstring format = imported.posterImageFormat;
    document_.updateProject([&](Project& project) {
        const std::wstring before = project.posterImageData ? *project.posterImageData : std::wstring{};
        const std::wstring after = data ? *data : std::wstring{};
        if (before == after && project.posterImageFormat == format) return false;
        project.posterImageData = data;
        project.posterImageFormat = format;
        return true;
    }, L"Изменение афиши");
    updatePoster();
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void ProjectEditorPanel::removePoster() {
    if (!document_.project().posterImageData) return;
    if (MessageBoxW(hwnd_, L"Удалить афишу из проекта?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    document_.updateProject([](Project& project) {
        if (!project.posterImageData) return false;
        PosterService::clear(project);
        return true;
    }, L"Удаление афиши");
    updatePoster();
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
}

void ProjectEditorPanel::updatePoster() {
    const Project& project = document_.project();
    const bool hasPoster = static_cast<bool>(project.posterImageData);
    SetWindowTextW(poster_, hasPoster ? L"✎  ИЗМЕНИТЬ АФИШУ" : L"+  ДОБАВИТЬ АФИШУ");
    const std::wstring status = hasPoster
        ? L"Встроена в проект · " + project.posterImageFormat
        : L"Афиша не выбрана";
    SetWindowTextW(posterStatus_, status.c_str());
    refreshPosterBitmap();
    InvalidateRect(poster_, nullptr, TRUE);
}

void ProjectEditorPanel::refreshPosterBitmap() {
    posterBitmap_.reset();
    if (!gdiplusToken_) return;
    posterBitmap_ = BitmapFromBytes(PosterService::decode(document_.project()));
}

bool ProjectEditorPanel::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON || draw->hwndItem != poster_) return false;
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
    float captionTop = height - 46.0f * uiScale_;

    if (posterBitmap_ && posterBitmap_->GetLastStatus() == Gdiplus::Ok) {
        const float sourceWidth = static_cast<float>(posterBitmap_->GetWidth());
        const float sourceHeight = static_cast<float>(posterBitmap_->GetHeight());
        const float scale = std::min(width / sourceWidth, height / sourceHeight);
        const float imageWidth = sourceWidth * scale;
        const float imageHeight = sourceHeight * scale;
        const Gdiplus::RectF imageBounds((width - imageWidth) * 0.5f, (height - imageHeight) * 0.5f,
                                         imageWidth, imageHeight);
        graphics.DrawImage(posterBitmap_.get(), imageBounds, 0.0f, 0.0f,
                           sourceWidth, sourceHeight, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush overlay(Color(theme_.panel));
        graphics.FillRectangle(&overlay, 0.0f, height - 46.0f * uiScale_, width, 46.0f * uiScale_);
    } else {
        Gdiplus::Pen frame(Color(theme_.textMuted), 2.0f);
        const float iconWidth = std::min({width * 0.32f, 72.0f * uiScale_, height * 0.22f});
        const float iconHeight = iconWidth * 1.35f;
        const float iconX = (width - iconWidth) * 0.5f;
        const float iconY = (height - iconHeight - 44.0f * uiScale_) * 0.5f;
        captionTop = iconY + iconHeight + 12.0f * uiScale_;
        graphics.DrawRectangle(&frame, iconX, iconY, iconWidth, iconHeight);
        graphics.DrawLine(&frame, iconX + iconWidth * 0.18f, iconY + iconHeight * 0.72f,
                          iconX + iconWidth * 0.45f, iconY + iconHeight * 0.42f);
        graphics.DrawLine(&frame, iconX + iconWidth * 0.45f, iconY + iconHeight * 0.42f,
                          iconX + iconWidth * 0.82f, iconY + iconHeight * 0.72f);
    }

    Gdiplus::Pen border(Color(focused ? theme_.accent : theme_.border), focused ? 2.0f : 1.0f);
    graphics.DrawRectangle(&border, 0.5f, 0.5f, std::max(0.0f, width - 1.0f), std::max(0.0f, height - 1.0f));
    Gdiplus::Font font(L"Segoe UI", 13.0f * uiScale_, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Color(theme_.text));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    const std::wstring caption = WindowText(poster_);
    const Gdiplus::RectF textBounds(8.0f, captionTop, std::max(0.0f, width - 16.0f),
                                    (posterBitmap_ ? 46.0f : 32.0f) * uiScale_);
    graphics.DrawString(caption.c_str(), -1, &font, textBounds, &format, &text);
    return true;
}

void ProjectEditorPanel::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    RECT rect{}; GetClientRect(hwnd_, &rect); onLayout(rect.right, rect.bottom);
    InvalidateRect(poster_, nullptr, TRUE);
}

}  // namespace mezozoy::ui
