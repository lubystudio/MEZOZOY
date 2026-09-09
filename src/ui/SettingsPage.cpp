#include "SettingsPage.h"

#include "Win32Util.h"
#include "UiStyle.h"

#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace mezozoy::ui {
namespace {

constexpr const wchar_t* CategoryRows[] = {
    L"Внешний вид", L"Редактор", L"Проекты", L"Сохранение", L"Поведение", L"Дополнительно"
};

constexpr const wchar_t* SectionTitles[] = {
    L"Внешний вид",
    L"Редактор сценария",
    L"Проекты",
    L"Сохранение и резервные копии",
    L"Поведение",
    L"Дополнительные настройки"
};

constexpr const wchar_t* SectionSubtitles[] = {
    L"Настройте оформление и плотность рабочего пространства",
    L"Шрифт, сценарные подсказки и поведение редактора",
    L"Папка библиотеки и восстановление последнего проекта",
    L"Автосохранение и защита данных проекта",
    L"Подтверждения, синхронизация и история действий",
    L"Диагностика и обслуживание конфигурации Mezozoy"
};

constexpr const wchar_t* AccentHex[] = {
    L"#8B5CF6", L"#EC4899", L"#A855F7", L"#4F46E5",
    L"#0EA5E9", L"#14B8A6", L"#22C55E", L"#F97316"
};

constexpr COLORREF AccentColors[] = {
    RGB(139, 92, 246), RGB(236, 72, 153), RGB(168, 85, 247), RGB(79, 70, 229),
    RGB(14, 165, 233), RGB(20, 184, 166), RGB(34, 197, 94), RGB(249, 115, 22)
};

constexpr float ScaleValues[] = {0.90f, 1.00f, 1.10f, 1.25f};
constexpr const wchar_t* ScaleTitles[] = {L"90%", L"100%", L"110%", L"125%"};
constexpr const wchar_t* ScaleNotes[] = {L"Мелкий", L"Обычный", L"Крупный", L"Очень крупный"};

int Width(const RECT& rect) { return rect.right - rect.left; }
int Height(const RECT& rect) { return rect.bottom - rect.top; }

void FillRounded(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius, int borderWidth = 1) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
    const HGDIOBJ oldBrush = SelectObject(dc, brush);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void FillSolid(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawLabel(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color,
               UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &rect, format);
    SelectObject(dc, oldFont);
}

}  // namespace

SettingsPage::SettingsPage(HINSTANCE instance, HWND parent, SettingsService& settings, const Theme& theme)
    : NativePage(instance, parent, theme), settings_(settings) {
    categories_ = CreateChild(L"LISTBOX", L"",
                              LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP,
                              IdCategories, hwnd_, instance_);
    SetPropW(categories_,L"Mezozoy.Ui.List",reinterpret_cast<HANDLE>(1));
    for (const wchar_t* row : CategoryRows)
        SendMessageW(categories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
    SetWindowTheme(categories_, L"", L"");

    title_ = CreateChild(L"STATIC", L"Настройки", SS_LEFT, 0, hwnd_, instance_);
    subtitle_ = CreateChild(L"STATIC", L"Персонализируйте внешний вид и поведение приложения", SS_LEFT, 0, hwnd_, instance_);
    sectionTitle_ = CreateChild(L"STATIC", SectionTitles[0], SS_LEFT, 0, hwnd_, instance_);
    sectionSubtitle_ = CreateChild(L"STATIC", SectionSubtitles[0], SS_LEFT, 0, hwnd_, instance_);
    remember(categories_);
    remember(title_, true);
    remember(subtitle_);
    remember(sectionTitle_, true);
    remember(sectionSubtitle_);

    themeLabel_ = label(L"Тема приложения", 0, true);
    themeDark_ = ownerButton(L"Графит", IdThemeDark, 0);
    themeDarker_ = ownerButton(L"Глубокий графит", IdThemeDarker, 0);
    themeLight_ = ownerButton(L"Молочно-белая", IdThemeLight, 0);
    accentLabel_ = label(L"Акцентный цвет", 0, true);
    for (std::size_t index = 0; index < accentButtons_.size(); ++index)
        accentButtons_[index] = ownerButton(L"", IdAccentFirst + static_cast<int>(index), 0);
    accentHexLabel_ = label(L"HEX", 0);
    accent_ = edit(settings_.get().accentColorHex, IdAccent, 0);
    scaleLabel_ = label(L"Масштаб интерфейса", 0, true);
    for (std::size_t index = 0; index < scaleButtons_.size(); ++index)
        scaleButtons_[index] = ownerButton(L"", IdScaleFirst + static_cast<int>(index), 0);
    compact_ = check(L"Компактный режим — уменьшить отступы и размеры панелей", IdCompact, 0);

    label(L"Шрифт сценария", 1, true);
    font_ = edit(settings_.get().scriptFont, IdFont, 1);
    label(L"Размер шрифта", 1, true);
    fontSize_ = edit(std::to_wstring(static_cast<int>(settings_.get().scriptFontSize)), IdFontSize, 1, true);
    SendMessageW(font_, EM_SETREADONLY, TRUE, 0);
    SendMessageW(fontSize_, EM_SETREADONLY, TRUE, 0);
    pageView_ = check(L"Использовать вид страницы", IdPageView, 1);
    lineType_ = check(L"Показывать тип текущей строки", IdLineType, 1);
    autocomplete_ = check(L"Включить автодополнение персонажей и заголовков", IdAutocomplete, 1);
    smartEnter_ = check(L"Умный Enter для перехода между элементами сценария", IdSmartEnter, 1);
    showCounters_ = check(L"Показывать счётчики слов, страниц и минут", IdShowCounters, 1);

    label(L"Папка проектов", 2, true);
    projectFolder_ = edit(settings_.get().projectsFolder, IdProjectFolder, 2);
    browse_ = ownerButton(L"Выбрать папку", IdBrowse, 2);
    openProjectFolder_ = ownerButton(L"Открыть папку проектов", IdOpenProjectFolder, 2);
    restore_ = check(L"Открывать последний проект при запуске", IdRestore, 2);

    autosave_ = check(L"Включить автосохранение", IdAutosave, 3);
    label(L"Интервал автосохранения, секунд", 3, true);
    autosaveSeconds_ = edit(std::to_wstring(settings_.get().autosaveSeconds), IdAutosaveSeconds, 3, true);
    backups_ = check(L"Создавать резервные копии проекта", IdBackups, 3);
    label(L"Максимальное количество резервных копий", 3, true);
    backupCount_ = edit(std::to_wstring(settings_.get().maxBackups), IdBackupCount, 3, true);

    confirmScene_ = check(L"Подтверждать удаление сцены", IdConfirmScene, 4);
    confirmCard_ = check(L"Подтверждать удаление карточки", IdConfirmCard, 4);
    selectMatchingCard_ = check(L"Выделять карточку при выборе сцены", IdSelectMatchingCard, 4);
    selectMatchingScene_ = check(L"Выделять сцену при выборе карточки", IdSelectMatchingScene, 4);
    keepSync_ = check(L"Синхронизировать сценарий и карточки", IdKeepSync, 4);
    askSave_ = check(L"Спрашивать о сохранении перед закрытием или сменой проекта", IdAskSave, 4);
    label(L"Количество сохраняемых шагов истории (0 — отключить)", 4, true);
    historySteps_ = edit(std::to_wstring(settings_.get().historySteps), IdHistorySteps, 4, true);

    diagnostics_ = check(L"Показывать диагностический оверлей карточной доски", IdDiagnostics, 5);
    label(L"Файл настроек", 5, true);
    settingsPath_ = edit(settings_.path().wstring(), 0, 5);
    SendMessageW(settingsPath_, EM_SETREADONLY, TRUE, 0);
    openSettingsFile_ = ownerButton(L"Открыть файл настроек", IdOpenSettingsFile, 5);
    openSettingsFolder_ = ownerButton(L"Открыть папку настроек", IdOpenSettingsFolder, 5);
    reset_ = ownerButton(L"Сбросить настройки", IdReset, 5);

    SendMessageW(categories_, LB_SETCURSEL, 0, 0);
    load();
    showGroup(0);
}

HWND SettingsPage::label(const wchar_t* text, int group, bool bold) {
    HWND control = CreateChild(L"STATIC", text, SS_LEFT, 0, hwnd_, instance_);
    addTo(group, control);
    remember(control, bold);
    return control;
}

HWND SettingsPage::check(const wchar_t* text, int id, int group) {
    HWND control = CreateChild(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id, hwnd_, instance_);
    addTo(group, control);
    toggleStates_[control] = false;
    remember(control);
    return control;
}

HWND SettingsPage::edit(const std::wstring& text, int id, int group, bool number) {
    HWND control = CreateChild(L"EDIT", text.c_str(), ES_AUTOHSCROLL | WS_TABSTOP | (number ? ES_NUMBER : 0),
                               id, hwnd_, instance_, WS_EX_CLIENTEDGE);
    addTo(group, control);
    remember(control);
    return control;
}

HWND SettingsPage::ownerButton(const wchar_t* text, int id, int group) {
    HWND control = CreateChild(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id, hwnd_, instance_);
    addTo(group, control);
    remember(control);
    return control;
}

bool SettingsPage::isToggle(HWND control) const {
    return toggleStates_.find(control) != toggleStates_.end();
}

bool SettingsPage::isToggleChecked(HWND control) const {
    const auto found = toggleStates_.find(control);
    return found != toggleStates_.end() && found->second;
}

void SettingsPage::setToggleChecked(HWND control, bool checked) {
    const auto found = toggleStates_.find(control);
    if (found == toggleStates_.end()) return;
    found->second = checked;
    InvalidateRect(control, nullptr, TRUE);
}

void SettingsPage::addTo(int group, HWND control) { groups_[group].push_back(control); }

void SettingsPage::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    SendMessageW(categories_, LB_SETITEMHEIGHT, 0, std::max(38, static_cast<int>(44.0f * uiScale_)));
    invalidateVisuals();
}

HBRUSH SettingsPage::controlColor(HDC dc, HWND control, UINT message) {
    if (control == categories_) {
        SetTextColor(dc, theme_.text);
        SetBkColor(dc, theme_.navigation);
        SetDCBrushColor(dc, theme_.navigation);
        return reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    }
    if (message == WM_CTLCOLORSTATIC) {
        const bool pageHeader = control == title_ || control == subtitle_ ||
                                control == sectionTitle_ || control == sectionSubtitle_;
        SetTextColor(dc, (control == subtitle_ || control == sectionSubtitle_) ? theme_.textMuted : theme_.text);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, pageHeader ? theme_.background : theme_.panel);
        return pageHeader ? backgroundBrush_ : panelBrush_;
    }
    return NativePage::controlColor(dc, control, message);
}

void SettingsPage::onLayout(int width, int height) {
    const auto scaled = [this](int value) {
        return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * uiScale_)));
    };

    const int navWidth = std::clamp(scaled(228), scaled(190), std::max(scaled(190), width / 4));
    SetWindowPos(categories_, nullptr, scaled(8), scaled(86), navWidth - scaled(16),
                 std::min(height - scaled(106), scaled(294)), SWP_NOZORDER);

    const int headerX = navWidth + scaled(26);
    SetWindowPos(title_, nullptr, headerX, scaled(18), width - headerX - scaled(26), scaled(34), SWP_NOZORDER);
    SetWindowPos(subtitle_, nullptr, headerX, scaled(52), width - headerX - scaled(26), scaled(24), SWP_NOZORDER);
    SetWindowPos(sectionTitle_, nullptr, headerX, scaled(92), width - headerX - scaled(26), scaled(28), SWP_NOZORDER);
    SetWindowPos(sectionSubtitle_, nullptr, headerX, scaled(120), width - headerX - scaled(26), scaled(22), SWP_NOZORDER);

    cardRects_.clear();
    noticeRect_ = {};

    const int contentTop = scaled(154);
    const int contentRight = scaled(24);
    const int available = std::max(scaled(300), width - headerX - contentRight);

    if (selectedGroup_ == 0) {
        const int mainWidth = available;
        const int mainHeight = std::min(std::max(scaled(410), height - contentTop - scaled(24)), scaled(500));
        RECT mainCard{headerX, contentTop, headerX + mainWidth, contentTop + mainHeight};
        cardRects_.push_back(mainCard);

        const int pad = scaled(18);
        const int innerX = mainCard.left + pad;
        const int innerWidth = Width(mainCard) - pad * 2;
        int y = mainCard.top + pad;
        SetWindowPos(themeLabel_, nullptr, innerX, y, innerWidth, scaled(22), SWP_NOZORDER);
        y += scaled(28);
        const int themeGap = scaled(12);
        const bool compactThemes = innerWidth < scaled(570);
        const int themeWidth = compactThemes ? innerWidth : (innerWidth - themeGap * 2) / 3;
        int themeIndex = 0;
        for (HWND button : {themeDarker_, themeDark_, themeLight_}) {
            SetWindowPos(button, nullptr, innerX + (compactThemes ? 0 : themeIndex * (themeWidth + themeGap)),
                         y + (compactThemes ? themeIndex * scaled(48) : 0), themeWidth,
                         scaled(compactThemes ? 42 : 100), SWP_NOZORDER);
            ++themeIndex;
        }
        y += scaled(compactThemes ? 156 : 118);

        SetWindowPos(accentLabel_, nullptr, innerX, y, innerWidth, scaled(22), SWP_NOZORDER);
        y += scaled(27);
        const int swatch = scaled(32);
        const int swatchGap = scaled(8);
        int swatchX = innerX;
        for (HWND button : accentButtons_) {
            SetWindowPos(button, nullptr, swatchX, y, swatch, swatch, SWP_NOZORDER);
            swatchX += swatch + swatchGap;
        }
        const int hexWidth = scaled(110);
        if (swatchX + hexWidth > innerX + innerWidth) y += scaled(58);
        SetWindowPos(accentHexLabel_, nullptr, innerX + innerWidth - hexWidth, y - scaled(22), hexWidth, scaled(20), SWP_NOZORDER);
        SetWindowPos(accent_, nullptr, innerX + innerWidth - hexWidth, y, hexWidth, swatch, SWP_NOZORDER);
        y += scaled(48);

        SetWindowPos(scaleLabel_, nullptr, innerX, y, innerWidth, scaled(22), SWP_NOZORDER);
        y += scaled(28);
        const int scaleGap = scaled(10);
        const int scaleWidth = (innerWidth - scaleGap * 3) / 4;
        for (std::size_t index = 0; index < scaleButtons_.size(); ++index)
            SetWindowPos(scaleButtons_[index], nullptr, innerX + static_cast<int>(index) * (scaleWidth + scaleGap), y,
                         scaleWidth, scaled(58), SWP_NOZORDER);
        y += scaled(72);
        SetWindowPos(compact_, nullptr, innerX, y, innerWidth, scaled(34), SWP_NOZORDER);

    } else {
        const int cardWidth = std::min(available, scaled(860));
        const int x = headerX;
        const int pad = scaled(20);
        int y = contentTop + pad;
        const int innerX = x + pad;
        const int innerWidth = cardWidth - pad * 2;
        const auto found = groups_.find(selectedGroup_);
        if (found != groups_.end()) {
            for (HWND control : found->second) {
                wchar_t className[32]{};
                GetClassNameW(control, className, static_cast<int>(std::size(className)));
                if (_wcsicmp(className, L"Static") == 0) {
                    SetWindowPos(control, nullptr, innerX, y, innerWidth, scaled(22), SWP_NOZORDER);
                    y += scaled(27);
                } else if (_wcsicmp(className, L"Edit") == 0) {
                    SetWindowPos(control, nullptr, innerX, y, innerWidth, scaled(38), SWP_NOZORDER);
                    y += scaled(52);
                } else {
                    const LONG_PTR type = GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK;
                    const bool command = type == BS_OWNERDRAW && !isToggle(control);
                    SetWindowPos(control, nullptr, innerX, y, command ? std::min(innerWidth, scaled(270)) : innerWidth,
                                 scaled(38), SWP_NOZORDER);
                    y += scaled(48);
                }
            }
        }
        const int bottom = std::min(height - scaled(22), y + pad - scaled(8));
        cardRects_.push_back(RECT{x, contentTop, x + cardWidth, std::max(contentTop + scaled(120), bottom)});
    }

    invalidateVisuals();
}

void SettingsPage::showGroup(int group) {
    selectedGroup_ = std::clamp(group, 0, 5);
    for (auto& [index, controls] : groups_)
        for (HWND control : controls) ShowWindow(control, index == selectedGroup_ ? SW_SHOW : SW_HIDE);
    SendMessageW(categories_, LB_SETCURSEL, selectedGroup_, 0);
    SetWindowTextW(sectionTitle_, SectionTitles[selectedGroup_]);
    SetWindowTextW(sectionSubtitle_, SectionSubtitles[selectedGroup_]);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
}

bool SettingsPage::handleCommand(int id, int code, HWND source) {
    if (id == IdCategories && code == LBN_SELCHANGE) {
        showGroup(static_cast<int>(SendMessageW(categories_, LB_GETCURSEL, 0, 0)));
        return true;
    }
    if (id == IdThemeDark && code == BN_CLICKED) {
        themeSelection_ = 0;
        save();
        return true;
    }
    if (id == IdThemeDarker && code == BN_CLICKED) {
        themeSelection_ = 1;
        save();
        return true;
    }
    if (id == IdThemeLight && code == BN_CLICKED) {
        themeSelection_ = 2;
        save();
        return true;
    }
    if (id >= IdAccentFirst && id < IdAccentFirst + static_cast<int>(accentButtons_.size()) && code == BN_CLICKED) {
        accentSelection_ = id - IdAccentFirst;
        SetWindowTextW(accent_, AccentHex[accentSelection_]);
        save();
        return true;
    }
    if (id >= IdScaleFirst && id < IdScaleFirst + static_cast<int>(scaleButtons_.size()) && code == BN_CLICKED) {
        scaleSelection_ = id - IdScaleFirst;
        save();
        return true;
    }
    if (code == BN_CLICKED && isToggle(source)) {
        setToggleChecked(source, !isToggleChecked(source));
        save();
        return true;
    }
    if (id == IdBrowse && code == BN_CLICKED) {
        browseFolder();
        return true;
    }
    if (id == IdOpenProjectFolder && code == BN_CLICKED) {
        const std::wstring folder = WindowText(projectFolder_);
        if (!folder.empty()) {
            std::error_code ignored;
            std::filesystem::create_directories(folder, ignored);
            ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return true;
    }
    if (id == IdOpenSettingsFile && code == BN_CLICKED) {
        settings_.save();
        ShellExecuteW(hwnd_, L"open", settings_.path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    if (id == IdOpenSettingsFolder && code == BN_CLICKED) {
        settings_.save();
        const auto folder = settings_.path().parent_path();
        ShellExecuteW(hwnd_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    if (id == IdReset && code == BN_CLICKED) {
        if (MessageBoxW(hwnd_, L"Сбросить все настройки?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            settings_.reset();
            load();
            if (apply_) apply_();
            showGroup(selectedGroup_);
        }
        return true;
    }
    if (code == BN_CLICKED || code == EN_KILLFOCUS) {
        save();
        return true;
    }
    return false;
}

LRESULT SettingsPage::onMessage(UINT message, WPARAM, LPARAM) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        drawPage(dc);
        EndPaint(hwnd_, &paint);
        return 1;
    }
    return 0;
}

void SettingsPage::commit() { save(); }

void SettingsPage::load() {
    loading_ = true;
    const auto& settings = settings_.get();
    themeSelection_ = settings.themeMode == L"Light" ? 2 : settings.themeMode == L"Darker" ? 1 : 0;
    accentSelection_ = -1;
    for (std::size_t index = 0; index < std::size(AccentHex); ++index)
        if (_wcsicmp(settings.accentColorHex.c_str(), AccentHex[index]) == 0)
            accentSelection_ = static_cast<int>(index);
    scaleSelection_ = 0;
    float nearest = 1000.0f;
    for (std::size_t index = 0; index < std::size(ScaleValues); ++index) {
        const float distance = std::fabs(settings.uiScale - ScaleValues[index]);
        if (distance < nearest) {
            nearest = distance;
            scaleSelection_ = static_cast<int>(index);
        }
    }
    SetWindowTextW(accent_, settings.accentColorHex.c_str());
    const std::pair<HWND, bool> checks[] = {
        {compact_, settings.compactMode}, {diagnostics_, settings.diagnostics}, {pageView_, settings.pageView},
        {lineType_, settings.lineTypeIndicator}, {autocomplete_, settings.autocomplete}, {smartEnter_, settings.smartEnter},
        {showCounters_, settings.showCounters}, {restore_, settings.restoreLastProject}, {autosave_, settings.autosave},
        {backups_, settings.backups}, {confirmScene_, settings.confirmDeleteScene}, {confirmCard_, settings.confirmDeleteCard},
        {selectMatchingCard_, settings.selectMatchingCard}, {selectMatchingScene_, settings.selectMatchingScene},
        {keepSync_, settings.keepScriptCardsSync}, {askSave_, settings.askSave}
    };
    for (const auto& [control, checked] : checks)
        setToggleChecked(control, checked);
    SetWindowTextW(font_, settings.scriptFont.c_str());
    SetWindowTextW(fontSize_, std::to_wstring(static_cast<int>(settings.scriptFontSize)).c_str());
    SetWindowTextW(projectFolder_, settings.projectsFolder.c_str());
    SetWindowTextW(autosaveSeconds_, std::to_wstring(settings.autosaveSeconds).c_str());
    SetWindowTextW(backupCount_, std::to_wstring(settings.maxBackups).c_str());
    SetWindowTextW(historySteps_, std::to_wstring(settings.historySteps).c_str());
    SetWindowTextW(settingsPath_, settings_.path().wstring().c_str());
    loading_ = false;
    invalidateVisuals();
}

void SettingsPage::save() {
    if (loading_) return;
    auto& settings = settings_.edit();
    settings.themeMode = themeSelection_ == 2 ? L"Light" : themeSelection_ == 1 ? L"Darker" : L"Dark";
    settings.accentColorHex = WindowText(accent_);
    settings.accentColorName = accentSelection_ == 0 ? L"Mezozoy Purple" : L"Custom";
    settings.uiScale = ScaleValues[std::clamp(scaleSelection_, 0, static_cast<int>(std::size(ScaleValues)) - 1)];
    const auto checked = [this](HWND control) { return isToggleChecked(control); };
    settings.compactMode = checked(compact_);
    settings.diagnostics = checked(diagnostics_);
    settings.scriptFont = L"Courier New";
    settings.scriptFontSize = 12.0f;
    settings.pageView = checked(pageView_);
    settings.lineTypeIndicator = checked(lineType_);
    settings.autocomplete = checked(autocomplete_);
    settings.smartEnter = checked(smartEnter_);
    settings.showCounters = checked(showCounters_);
    settings.projectsFolder = WindowText(projectFolder_);
    settings.restoreLastProject = checked(restore_);
    settings.autosave = checked(autosave_);
    settings.backups = checked(backups_);
    try { settings.autosaveSeconds = std::stoi(WindowText(autosaveSeconds_)); } catch (...) {}
    try { settings.maxBackups = std::stoi(WindowText(backupCount_)); } catch (...) {}
    settings.autosaveSeconds = std::clamp(settings.autosaveSeconds, 15, 600);
    settings.maxBackups = std::clamp(settings.maxBackups, 1, 100);
    settings.confirmDeleteScene = checked(confirmScene_);
    settings.confirmDeleteCard = checked(confirmCard_);
    settings.selectMatchingCard = checked(selectMatchingCard_);
    settings.selectMatchingScene = checked(selectMatchingScene_);
    settings.keepScriptCardsSync = checked(keepSync_);
    settings.askSave = checked(askSave_);
    try { settings.historySteps = std::stoi(WindowText(historySteps_)); } catch (...) {}
    settings.historySteps = std::clamp(settings.historySteps, 0, 1000);
    SetWindowTextW(fontSize_, std::to_wstring(static_cast<int>(settings.scriptFontSize)).c_str());
    SetWindowTextW(autosaveSeconds_, std::to_wstring(settings.autosaveSeconds).c_str());
    SetWindowTextW(backupCount_, std::to_wstring(settings.maxBackups).c_str());
    SetWindowTextW(historySteps_, std::to_wstring(settings.historySteps).c_str());
    settings_.save();
    if (apply_) apply_();
    invalidateVisuals();
}

void SettingsPage::browseFolder() {
    BROWSEINFOW info{};
    info.hwndOwner = hwnd_;
    info.lpszTitle = L"Выберите папку проектов";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE result = SHBrowseForFolderW(&info);
    if (!result) return;
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(result, path)) {
        SetWindowTextW(projectFolder_, path);
        save();
    }
    CoTaskMemFree(result);
}

void SettingsPage::invalidateVisuals() {
    if (!IsWindow(hwnd_)) return;
    InvalidateRect(hwnd_, nullptr, TRUE);
    InvalidateRect(categories_, nullptr, TRUE);
    for (HWND button : accentButtons_) InvalidateRect(button, nullptr, TRUE);
    for (HWND button : scaleButtons_) InvalidateRect(button, nullptr, TRUE);
    InvalidateRect(themeDark_, nullptr, TRUE);
    InvalidateRect(themeDarker_, nullptr, TRUE);
    InvalidateRect(themeLight_, nullptr, TRUE);
}

bool SettingsPage::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (!draw) return false;
    if (draw->CtlType == ODT_BUTTON) FillSolid(draw->hDC, draw->rcItem, theme_.panel);
    if (draw->CtlID == IdCategories && draw->CtlType == ODT_LISTBOX) {
        if (draw->itemID == static_cast<UINT>(-1)) return true;
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        FillSolid(draw->hDC, draw->rcItem, selected ? theme_.selection : theme_.navigation);
        if (selected) {
            RECT accentBar = draw->rcItem;
            accentBar.right = accentBar.left + std::max(3, static_cast<int>(3.0f * uiScale_));
            FillSolid(draw->hDC, accentBar, theme_.accent);
        }
        RECT textRect = draw->rcItem;
        textRect.left += std::max(14, static_cast<int>(15.0f * uiScale_));
        const wchar_t* text = draw->itemID < std::size(CategoryRows) ? CategoryRows[draw->itemID] : L"";
        DrawLabel(draw->hDC, text, textRect, selected ? uiFontBold_ : uiFont_, selected ? theme_.text : theme_.textMuted);
        return true;
    }

    if (draw->CtlID == IdThemeDark || draw->CtlID == IdThemeDarker || draw->CtlID == IdThemeLight) {
        const bool darker = draw->CtlID == IdThemeDarker;
        const bool light = draw->CtlID == IdThemeLight;
        const bool selected = themeSelection_ == (light ? 2 : darker ? 1 : 0);
        const Theme sampleTheme = Theme::FromMode(light ? L"Light" : darker ? L"Darker" : L"Dark",
                                                 Theme::ParseHex(settings_.get().accentColorHex));
        const wchar_t* title = light ? L"Молочно-белая" : darker ? L"Глубокий графит" : L"Графит";
        FillRounded(draw->hDC, draw->rcItem, theme_.panelAlt, selected ? theme_.accent : theme_.border,
                    std::max(8, static_cast<int>(8.0f * uiScale_)), selected ? 2 : 1);
        if (Height(draw->rcItem) < 70 * uiScale_) {
            RECT text = draw->rcItem; text.left += 14; text.right -= 32;
            DrawLabel(draw->hDC, title, text, uiFontBold_, theme_.text);
            if (selected) { RECT check = draw->rcItem; check.left = check.right - 30;
                DrawLabel(draw->hDC, L"✓", check, uiFontBold_, theme_.accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
            return true;
        }
        RECT sample = draw->rcItem;
        InflateRect(&sample, -8, -8);
        sample.bottom = sample.top + Height(draw->rcItem) * 40 / 100;
        FillRounded(draw->hDC, sample, sampleTheme.background, sampleTheme.border, 6);
        RECT miniNav = sample;
        miniNav.right = miniNav.left + std::max(18, Width(sample) / 5);
        miniNav.left += 4;
        miniNav.top += 4;
        miniNav.bottom -= 4;
        FillRounded(draw->hDC, miniNav, sampleTheme.navigation, sampleTheme.border, 4);
        RECT miniSelection = miniNav;
        miniSelection.left += 3;
        miniSelection.right -= 3;
        miniSelection.top += 9;
        miniSelection.bottom = miniSelection.top + 7;
        FillRounded(draw->hDC, miniSelection, sampleTheme.selection, sampleTheme.accent, 3);
        RECT line = sample;
        line.left = miniNav.right + 10;
        line.right -= 10;
        line.top += 12;
        line.bottom = line.top + 5;
        FillRounded(draw->hDC, line, sampleTheme.textMuted, sampleTheme.textMuted, 2);
        line.top += 12;
        line.bottom += 12;
        line.right -= Width(line) / 3;
        FillRounded(draw->hDC, line, sampleTheme.border, sampleTheme.border, 2);
        RECT titleRect = draw->rcItem;
        titleRect.left += 10;
        titleRect.right -= 10;
        titleRect.top = sample.bottom + 5;
        titleRect.bottom = titleRect.top + 22;
        DrawLabel(draw->hDC, title, titleRect, uiFontBold_, theme_.text);
        RECT noteRect = titleRect;
        noteRect.top += 20;
        noteRect.bottom += 20;
        DrawLabel(draw->hDC, light ? L"Светлая" : darker ? L"Максимально тёмный" : L"По умолчанию", noteRect, uiFont_, theme_.textMuted);
        if (selected) {
            RECT badge{draw->rcItem.right - 30, draw->rcItem.top + 8, draw->rcItem.right - 8, draw->rcItem.top + 30};
            FillRounded(draw->hDC, badge, theme_.accent, theme_.accent, 11);
            DrawLabel(draw->hDC, L"✓", badge, uiFontBold_, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return true;
    }

    if (draw->CtlID >= static_cast<UINT>(IdAccentFirst) &&
        draw->CtlID < static_cast<UINT>(IdAccentFirst + static_cast<int>(accentButtons_.size()))) {
        const int index = static_cast<int>(draw->CtlID) - IdAccentFirst;
        FillSolid(draw->hDC, draw->rcItem, theme_.panel);
        RECT circle = draw->rcItem;
        InflateRect(&circle, -4, -4);
        HBRUSH brush = CreateSolidBrush(AccentColors[index]);
        HPEN pen = CreatePen(PS_SOLID, index == accentSelection_ ? 3 : 1,
                             index == accentSelection_ ? theme_.text : AccentColors[index]);
        const HGDIOBJ oldBrush = SelectObject(draw->hDC, brush);
        const HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
        Ellipse(draw->hDC, circle.left, circle.top, circle.right, circle.bottom);
        SelectObject(draw->hDC, oldPen);
        SelectObject(draw->hDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        return true;
    }

    if (draw->CtlID >= static_cast<UINT>(IdScaleFirst) &&
        draw->CtlID < static_cast<UINT>(IdScaleFirst + static_cast<int>(scaleButtons_.size()))) {
        const int index = static_cast<int>(draw->CtlID) - IdScaleFirst;
        const bool selected = index == scaleSelection_;
        FillRounded(draw->hDC, draw->rcItem, selected ? theme_.selection : theme_.panelAlt,
                    selected ? theme_.accent : theme_.border, 7, selected ? 2 : 1);
        RECT titleRect = draw->rcItem;
        titleRect.bottom = titleRect.top + Height(draw->rcItem) / 2 + 5;
        DrawLabel(draw->hDC, ScaleTitles[index], titleRect, uiFontBold_, theme_.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT noteRect = draw->rcItem;
        noteRect.top += Height(draw->rcItem) / 2 + 3;
        DrawLabel(draw->hDC, ScaleNotes[index], noteRect, uiFont_, theme_.textMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return true;
    }

    if (draw->CtlType == ODT_BUTTON && isToggle(draw->hwndItem)) {
        const bool checked = isToggleChecked(draw->hwndItem);
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
        const COLORREF rowFill = pressed ? theme_.selection : theme_.panelAlt;
        FillRounded(draw->hDC, draw->rcItem, rowFill, theme_.border,
                    std::max(7, static_cast<int>(7.0f * uiScale_)));

        const int switchWidth = std::max(38, static_cast<int>(40.0f * uiScale_));
        const int switchHeight = std::max(20, static_cast<int>(22.0f * uiScale_));
        RECT track{
            draw->rcItem.right - switchWidth - std::max(10, static_cast<int>(12.0f * uiScale_)),
            draw->rcItem.top + (Height(draw->rcItem) - switchHeight) / 2,
            draw->rcItem.right - std::max(10, static_cast<int>(12.0f * uiScale_)),
            draw->rcItem.top + (Height(draw->rcItem) + switchHeight) / 2
        };
        FillRounded(draw->hDC, track, checked ? theme_.accent : theme_.border,
                    checked ? theme_.accent : theme_.textMuted, switchHeight / 2);
        const int knobSize = switchHeight - 6;
        const int knobLeft = checked ? track.right - knobSize - 3 : track.left + 3;
        RECT knob{knobLeft, track.top + 3, knobLeft + knobSize, track.bottom - 3};
        HBRUSH knobBrush = CreateSolidBrush(disabled ? theme_.textMuted : RGB(249, 250, 251));
        HPEN knobPen = CreatePen(PS_SOLID, 1, disabled ? theme_.textMuted : RGB(249, 250, 251));
        const HGDIOBJ oldBrush = SelectObject(draw->hDC, knobBrush);
        const HGDIOBJ oldPen = SelectObject(draw->hDC, knobPen);
        Ellipse(draw->hDC, knob.left, knob.top, knob.right, knob.bottom);
        SelectObject(draw->hDC, oldPen);
        SelectObject(draw->hDC, oldBrush);
        DeleteObject(knobPen);
        DeleteObject(knobBrush);

        RECT textRect = draw->rcItem;
        textRect.left += std::max(12, static_cast<int>(13.0f * uiScale_));
        textRect.right = track.left - std::max(8, static_cast<int>(10.0f * uiScale_));
        DrawLabel(draw->hDC, WindowText(draw->hwndItem), textRect, uiFont_,
                  disabled ? theme_.textMuted : theme_.text);
        return true;
    }

    if (draw->CtlType == ODT_BUTTON) { style::Button(draw, theme_, theme_.panel); return true; }
    return false;
}

void SettingsPage::drawPage(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    FillSolid(dc, client, theme_.background);

    const int navWidth = std::clamp(static_cast<int>(228.0f * uiScale_), static_cast<int>(190.0f * uiScale_),
                                    std::max(static_cast<int>(190.0f * uiScale_), static_cast<int>(client.right / 4)));
    RECT navigation{0, 0, navWidth, client.bottom};
    FillSolid(dc, navigation, theme_.navigation);
    RECT divider{navWidth - 1, 0, navWidth, client.bottom};
    FillSolid(dc, divider, theme_.border);

    for (const RECT& card : cardRects_)
        FillRounded(dc, card, theme_.panel, theme_.border, std::max(10, static_cast<int>(10.0f * uiScale_)));

    if (Width(noticeRect_) > 0) {
        FillRounded(dc, noticeRect_, theme_.panel, theme_.border, 10);
        RECT iconRect = noticeRect_;
        iconRect.left += 14;
        iconRect.right = iconRect.left + 24;
        DrawLabel(dc, L"ⓘ", iconRect, uiFontBold_, theme_.accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT titleRect = noticeRect_;
        titleRect.left += 48;
        titleRect.right -= 12;
        titleRect.top += 8;
        titleRect.bottom = titleRect.top + 22;
        DrawLabel(dc, L"Изменения применяются автоматически", titleRect, uiFontBold_, theme_.text);
        RECT noteRect = titleRect;
        noteRect.top += 22;
        noteRect.bottom += 22;
        DrawLabel(dc, L"Настройки сохраняются сразу и не требуют перезапуска.", noteRect, uiFont_, theme_.textMuted);
    }
}


}  // namespace mezozoy::ui
