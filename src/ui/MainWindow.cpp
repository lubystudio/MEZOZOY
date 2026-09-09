#include "MainWindow.h"

#include "Dialogs.h"
#include "RecoveryDialog.h"
#include "Win32Util.h"
#include "UiStyle.h"
#include "../core/AppVersion.h"
#include "../core/Utf.h"
#include "../services/CrashReporter.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mezozoy::ui {
namespace {

constexpr wchar_t MainClass[] = L"Mezozoy.MainWindow";

Theme ThemeFromSettings(const AppSettings& settings) {
    return Theme::FromMode(settings.themeMode, Theme::ParseHex(settings.accentColorHex));
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {
    settings_.load();
    document_.setHistoryLimit(static_cast<std::size_t>(settings_.get().historySteps));
    document_.setBackupPolicy(settings_.get().backups, static_cast<std::size_t>(settings_.get().maxBackups));
    theme_ = ThemeFromSettings(settings_.get());
}

MainWindow::~MainWindow() {
    if (updateThread_.joinable()) updateThread_.join();
    settingsPage_.reset(); script_.reset(); cards_.reset(); development_.reset(); home_.reset();
    if (uiFont_) DeleteObject(uiFont_); if (navFont_) DeleteObject(navFont_); if (brandFont_) DeleteObject(brandFont_);
    if (backgroundBrush_) DeleteObject(backgroundBrush_); if (navigationBrush_) DeleteObject(navigationBrush_); if (panelBrush_) DeleteObject(panelBrush_);
    if (updateBrush_) DeleteObject(updateBrush_);
}

bool MainWindow::create(const std::filesystem::path& startupFile) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = WindowProc; wc.hInstance = instance_; wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr; wc.lpszClassName = MainClass; wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(0, MainClass, AppNameAndVersion, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                            1500, 930, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    buildShell(); createPages(); registerFileAssociation();
    if (!startupFile.empty()) openPath(startupFile);
    else if (settings_.get().restoreLastProject && !settings_.get().lastProject.empty() && std::filesystem::exists(settings_.get().lastProject)) openPath(settings_.get().lastProject);
    else switchTo(View::Home);
    SetTimer(hwnd_, AutosaveTimer, static_cast<UINT>(settings_.get().autosaveSeconds * 1000), nullptr);
    checkForUpdates();
    return true;
}

int MainWindow::run(int showCommand) {
    ShowWindow(hwnd_, showCommand); UpdateWindow(hwnd_); MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // Commands must also work while focus belongs to a child edit control.
        // Text-editing shortcuts are deliberately not handled by globalShortcut().
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            if (message.wParam == VK_CONTROL) controlKeyDown_ = true;
            if (message.wParam == VK_SHIFT) shiftKeyDown_ = true;
            if (globalShortcut(message.wParam)) continue;
        } else if (message.message == WM_KEYUP || message.message == WM_SYSKEYUP) {
            if (message.wParam == VK_CONTROL) controlKeyDown_ = false;
            if (message.wParam == VK_SHIFT) shiftKeyDown_ = false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); self->hwnd_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case QueryAccentMessage:
            return static_cast<LRESULT>(theme_.accent);
        case QuerySelectionMessage:
            return static_cast<LRESULT>(theme_.selection);
        case UpdateResultMessage: {
            std::unique_ptr<UpdateInfo> info(reinterpret_cast<UpdateInfo*>(lParam));
            if (info) showUpdate(std::move(*info));
            return 0;
        }
        case WM_SHOWWINDOW:
            if (wParam && updateBanner_) {
                layout(true, true);
                RedrawWindow(hwnd_, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            break;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            layout();
            return 0;
        case WM_DISPLAYCHANGE:
            layout(true, true);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
            }
            if (script_) script_->applyAppearance(theme_, settings_.get().uiScale);
            layout(true, true);
            RedrawWindow(hwnd_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 1040;
            info->ptMinTrackSize.y = 680;
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT r{}; GetClientRect(hwnd_, &r);
            FillRect(reinterpret_cast<HDC>(wParam), &r, backgroundBrush_);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, theme_.text);
            SetBkMode(dc, TRANSPARENT);
            const HWND control = reinterpret_cast<HWND>(lParam);
            if (control == navigation_ || control == brand_) return reinterpret_cast<LRESULT>(navigationBrush_);
            if (control == updateBanner_ || control == updateLabel_) return reinterpret_cast<LRESULT>(updateBrush_);
            if (control == topBar_ || control == projectLabel_) return reinterpret_cast<LRESULT>(panelBrush_);
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.text); SetBkMode(dc, TRANSPARENT);
            const HWND control = reinterpret_cast<HWND>(lParam);
            if (control == updateDownload_ || control == updateLater_) return reinterpret_cast<LRESULT>(updateBrush_);
            return reinterpret_cast<LRESULT>(std::find(topButtons_.begin(), topButtons_.end(), control) != topButtons_.end() ? panelBrush_ : navigationBrush_);
        }
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw && (draw->hwndItem == updateDownload_ || draw->hwndItem == updateLater_))
                style::Button(draw, theme_, theme_.panelAlt);
            else if (draw && std::find(topButtons_.begin(), topButtons_.end(), draw->hwndItem) != topButtons_.end()) paintTopButton(draw);
            else paintNavigation(draw);
            return TRUE;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == BN_CLICKED) {
                switch (LOWORD(wParam)) {
                    case IdHome: switchTo(View::Home); return 0;
                    case IdDevelopment: switchTo(View::Development); return 0; case IdCards: switchTo(View::Cards); return 0;
                    case IdScript: switchTo(View::Script); return 0; case IdSettings: switchTo(View::Settings); return 0;
                    case IdSave: saveProject(false); return 0; case IdSaveAs: saveProject(true); return 0;
                    case IdPreview: showPreview(); return 0;
                    case IdImport: importScript(); return 0; case IdExport: exportScript(); return 0;
                    case IdProjectSearch: projectSearch(); return 0; case IdCommandPalette: commandPalette(); return 0;
                    case IdUndo: undoProject(); return 0; case IdRedo: redoProject(); return 0;
                    case IdUpdateDownload:
                        if (!updateUrl_.empty()) ShellExecuteW(hwnd_, L"open", updateUrl_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        return 0;
                    case IdUpdateLater: hideUpdate(); return 0;
                }
            }
            break;
        }
        case WM_KEYDOWN: if (globalShortcut(wParam)) return 0; break;
        case WM_TIMER:
            if (wParam == AutosaveTimer && settings_.get().autosave && document_.dirty()) {
                if (current_) current_->commit(); std::wstring error; if (document_.autosave(&error)) setStatus(L"Автосохранение выполнено"); else if (!error.empty()) setStatus(error);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (!closing_) { if (!confirmReplace()) return 0; closing_ = true; settings_.save(); }
            DestroyWindow(hwnd_); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void MainWindow::buildShell() {
    backgroundBrush_ = CreateSolidBrush(theme_.background); navigationBrush_ = CreateSolidBrush(theme_.navigation); panelBrush_ = CreateSolidBrush(theme_.panel);
    updateBrush_ = CreateSolidBrush(theme_.panelAlt);
    uiFont_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    navFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    brandFont_ = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    navigation_ = CreateChild(L"STATIC", L"", SS_LEFT, 0, hwnd_, instance_);
    topBar_ = CreateChild(L"STATIC", L"", SS_LEFT, 0, hwnd_, instance_);
    content_ = CreateChild(L"STATIC", L"", SS_LEFT, 0, hwnd_, instance_);
    status_ = CreateChild(L"STATIC", L"Готово", SS_LEFT, IdStatus, hwnd_, instance_); SetFont(status_, uiFont_);
    const std::wstring brandText = L"M   " + std::wstring(AppNameAndVersion);
    brand_ = CreateChild(L"STATIC", brandText.c_str(), SS_LEFT | SS_CENTERIMAGE, 0, hwnd_, instance_); SetFont(brand_, brandFont_);
    projectLabel_ = CreateChild(L"STATIC", L"Проект: Новый проект", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, 0, hwnd_, instance_); SetFont(projectLabel_, uiFont_);
    updateBanner_ = CreateChild(L"STATIC", L"", SS_LEFT | WS_CLIPSIBLINGS, 0, hwnd_, instance_);
    updateLabel_ = CreateChild(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS | WS_CLIPSIBLINGS, 0, hwnd_, instance_); SetFont(updateLabel_, uiFont_);
    updateDownload_ = CreateChild(L"BUTTON", L"Скачать с GitHub", BS_OWNERDRAW | WS_TABSTOP | WS_CLIPSIBLINGS, IdUpdateDownload, hwnd_, instance_); SetFont(updateDownload_, uiFont_); style::Attach(updateDownload_, &theme_);
    updateLater_ = CreateChild(L"BUTTON", L"Позже", BS_OWNERDRAW | WS_TABSTOP | WS_CLIPSIBLINGS, IdUpdateLater, hwnd_, instance_); SetFont(updateLater_, uiFont_); style::Attach(updateLater_, &theme_);
    for (HWND control : {updateBanner_, updateLabel_, updateDownload_, updateLater_}) ShowWindow(control, SW_HIDE);
    const std::pair<const wchar_t*, int> items[] = {
        {L"Начало", IdHome}, {L"Разработка", IdDevelopment}, {L"Карточки", IdCards},
        {L"Сценарий", IdScript}, {L"Настройки", IdSettings}
    };
    for (const auto& item : items) { HWND button = CreateChild(L"BUTTON", item.first, BS_OWNERDRAW | WS_TABSTOP, item.second, hwnd_, instance_); SetFont(button, navFont_); style::Attach(button,&theme_); navButtons_.push_back(button); }
    const std::pair<int, const wchar_t*> tools[] = {
        {IdUndo, L"Отменить (Ctrl+Z)"}, {IdRedo, L"Повторить (Ctrl+Y)"},
        {IdSave, L"Сохранить (Ctrl+S)"}, {IdProjectSearch, L"Поиск по проекту (Ctrl+Shift+F)"}
    };
    for (const auto& tool : tools) {
        HWND button = CreateChild(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, tool.first, hwnd_, instance_);
        style::Attach(button,&theme_);
        SetFont(button, uiFont_);
        topButtons_.push_back(button);
        addTooltip(button, tool.second);
    }
    document_.subscribe([this](DocumentChange change) {
        if (change == DocumentChange::Content || change == DocumentChange::Saved || change == DocumentChange::Replaced) {
            updateTitle();
            updateShellText();
        }
    });
    updateShellText();
}

void MainWindow::createPages() {
    home_ = std::make_unique<HomePage>(instance_, content_, document_, settings_, theme_);
    development_ = std::make_unique<DevelopmentPage>(instance_, content_, document_, theme_);
    cards_ = std::make_unique<CardsPage>(instance_, content_, document_, settings_, theme_);
    script_ = std::make_unique<ScriptPage>(instance_, content_, document_, settings_, theme_);
    settingsPage_ = std::make_unique<SettingsPage>(instance_, content_, settings_, theme_);
    home_->setCreateAction([this] { newProject(); }); home_->setOpenAction([this] { openProject(); });
    home_->setOpenPathAction([this](const auto& path) { if (confirmReplace()) openPath(path); });
    home_->setSaveAction([this] { return saveProject(false); });
    home_->setOpenScriptAction([this] { switchTo(View::Script); });
    home_->setOpenCardsAction([this] { switchTo(View::Cards); });
    home_->setOpenDevelopmentAction([this] { switchTo(View::Development); });
    home_->setNewSceneAction([this] {
        document_.addScene();
        setStatus(L"Добавлена новая сцена");
        switchTo(View::Script);
    });
    home_->setOpenSceneAction([this](std::size_t index) {
        document_.setSelectedScene(index);
        switchTo(View::Script);
    });
    script_->setSaveAction([this] { return saveProject(false); });
    cards_->setOpenSceneAction([this] { switchTo(View::Script); });
    cards_->setStatusCallback([this](const std::wstring& text) { setStatus(text); });
    settingsPage_->setApplyAction([this] { applySettings(); });
    for (auto* page : {static_cast<NativePage*>(home_.get()), static_cast<NativePage*>(development_.get()),
                       static_cast<NativePage*>(cards_.get()), static_cast<NativePage*>(script_.get()), static_cast<NativePage*>(settingsPage_.get())}) {
        page->applyAppearance(theme_, settings_.get().uiScale);
        page->show(false);
    }
    workspace_.registerPanel({L"home", L"Начало", home_.get(), 620, 420, false});
    workspace_.registerPanel({L"development", L"Разработка", development_.get(), 680, 420, false});
    workspace_.registerPanel({L"cards", L"Карточки", cards_.get(), 640, 420, false});
    workspace_.registerPanel({L"script", L"Сценарий", script_.get(), 720, 480, false});
    workspace_.registerPanel({L"settings", L"Настройки", settingsPage_.get(), 760, 520, false});
}

void MainWindow::layout(bool includeWorkspace, bool force) {
    if (!hwnd_) return;
    RECT r{};
    GetClientRect(hwnd_, &r);
    const int width = std::max(0L, r.right - r.left);
    const int height = std::max(0L, r.bottom - r.top);
    const bool sizeChanged = width != lastLayoutWidth_ || height != lastLayoutHeight_;
    if (!force && !sizeChanged && (!includeWorkspace || !workspaceLayoutPending_)) return;

    const float scale = settings_.get().uiScale;
    const int nav = std::clamp(static_cast<int>(std::lround(214.0f * scale)), 188, 286);
    const int topBarHeight = std::clamp(static_cast<int>(std::lround(56.0f * scale)), 50, 72);
    const int statusHeight = std::clamp(static_cast<int>(std::lround(25.0f * scale)), 22, 34);
    const int contentWidth = std::max(0, static_cast<int>(r.right) - nav);
    const int updateHeight = updateVisible_ ? std::clamp(static_cast<int>(std::lround(48.0f * scale)), 44, 60) : 0;
    const int contentHeight = std::max(0, static_cast<int>(r.bottom) - topBarHeight - updateHeight - statusHeight);

    const UINT shellFlags = SWP_NOZORDER | SWP_NOACTIVATE;
    SetWindowPos(navigation_, nullptr, 0, 0, nav, r.bottom - statusHeight, shellFlags);
    SetWindowPos(brand_, nullptr, 18, 0, nav - 28, topBarHeight, shellFlags);
    SetWindowPos(topBar_, nullptr, nav, 0, contentWidth, topBarHeight, shellFlags);
    SetWindowPos(projectLabel_, nullptr, nav + 22, 0, std::max(120, contentWidth - 260), topBarHeight, shellFlags);
    SetWindowPos(content_, nullptr, nav, topBarHeight + updateHeight, contentWidth, contentHeight, shellFlags);
    SetWindowPos(status_, nullptr, nav + 8, r.bottom - statusHeight, std::max(0, contentWidth - 12), statusHeight, shellFlags);

    const int buttonHeight = std::clamp(static_cast<int>(std::lround(48.0f * scale)), 44, 60);
    const int gap = std::clamp(static_cast<int>(std::lround(4.0f * scale)), 3, 8);
    const int start = topBarHeight + 14;
    for (std::size_t i = 0; i < navButtons_.size(); ++i)
        SetWindowPos(navButtons_[i], nullptr, 8, start + static_cast<int>(i) * (buttonHeight + gap), nav - 16, buttonHeight, shellFlags);

    const int toolSize = std::clamp(static_cast<int>(std::lround(38.0f * scale)), 34, 48);
    const int toolGap = 6;
    int toolX = r.right - 14 - static_cast<int>(topButtons_.size()) * toolSize - (static_cast<int>(topButtons_.size()) - 1) * toolGap;
    for (HWND button : topButtons_) {
        SetWindowPos(button, nullptr, toolX, (topBarHeight - toolSize) / 2, toolSize, toolSize, shellFlags);
        toolX += toolSize + toolGap;
    }
    if (updateVisible_) {
        const int buttonHeight = std::max(30, updateHeight - 12);
        const int laterWidth = 82;
        const int downloadWidth = 164;
        SetWindowPos(updateBanner_, HWND_BOTTOM, nav, topBarHeight, contentWidth, updateHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(updateLabel_, HWND_TOP, nav + 22, topBarHeight,
                     std::max(80, contentWidth - downloadWidth - laterWidth - 54), updateHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(updateDownload_, HWND_TOP, r.right - 22 - laterWidth - downloadWidth, topBarHeight + 6,
                     downloadWidth, buttonHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(updateLater_, HWND_TOP, r.right - 14 - laterWidth, topBarHeight + 6,
                     laterWidth, buttonHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        for (HWND control : {updateBanner_, updateLabel_, updateDownload_, updateLater_}) ShowWindow(control, SW_HIDE);
    }
    lastLayoutWidth_ = width;
    lastLayoutHeight_ = height;
    if (includeWorkspace) {
        workspace_.layout(contentWidth, contentHeight);
        workspaceLayoutPending_ = false;
    } else {
        workspaceLayoutPending_ = true;
    }
}

void MainWindow::switchTo(View view) {
    if (current_) current_->commit();
    view_ = view;
    std::wstring panelId;
    switch (view) {
        case View::Home: panelId = L"home"; break;
        case View::Development: panelId = L"development"; break;
        case View::Cards: panelId = L"cards"; break;
        case View::Script: panelId = L"script"; break;
        case View::Settings: panelId = L"settings"; break;
    }

    const WorkspacePanelDefinition* targetDefinition = workspace_.panel(panelId);
    Page* targetPage = targetDefinition ? targetDefinition->page : nullptr;
    ScopedRedrawLock contentRedraw(content_);
    ScopedRedrawLock targetPageRedraw(targetPage && targetPage != current_ ? targetPage->hwnd() : nullptr);

    switch (view) {
        case View::Home:
            home_->setProjectActive(projectActive_);
            home_->refreshRecent();
            break;
        case View::Development: development_->refreshTree(); break;
        case View::Script: script_->refreshFromDocument(); break;
        case View::Cards:
        case View::Settings:
            break;
    }
    workspace_.showSingle(panelId);
    current_ = workspace_.activePage();
    layout(true, true);
    if (current_) BringWindowToTop(current_->hwnd());

    targetPageRedraw.release(false);
    contentRedraw.release();
    for (HWND button : navButtons_) InvalidateRect(button, nullptr, TRUE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::setStatus(const std::wstring& text) { SetWindowTextW(status_, text.c_str()); }

bool MainWindow::confirmReplace() {
    if (current_) current_->commit(); if (!document_.dirty() || !settings_.get().askSave) return true;
    const int answer = MessageBoxW(hwnd_, L"В проекте есть несохраненные изменения. Сохранить их?", L"Mezozoy", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false; if (answer == IDYES) return saveProject(false); return true;
}

void MainWindow::newProject() {
    if (!confirmReplace()) return;
    document_.createNew();
    projectActive_ = true;
    home_->setProjectActive(true);
    setStatus(L"Создан новый проект");
    updateShellText();
    switchTo(View::Development);
}

void MainWindow::openProject() {
    if (!confirmReplace()) return; const auto file = OpenFileDialog(hwnd_, false, L"Проект Mezozoy (*.mzoy)\0*.mzoy\0Все файлы (*.*)\0*.*\0\0", L"mzoy");
    if (!file.empty()) openPath(file);
}

bool MainWindow::openPath(const std::filesystem::path& path) {
    const RecoveryScan scan = recovery_.scan(path);
    std::filesystem::path selectedPath = path;
    bool recovered = false;
    if (scan.needsRecovery) {
        const RecoveryDialogResult choice = ShowRecoveryDialog(hwnd_, scan, theme_);
        if (!choice.accepted) return false;
        selectedPath = choice.selectedPath;
        recovered = choice.recovered;
    }

    std::wstring error;
    const bool opened = recovered ? document_.openRecovered(path, selectedPath, &error) : document_.open(selectedPath, &error);
    if (!opened) {
        if (error.empty() && !scan.candidates.empty()) error = scan.candidates.front().error;
        MessageBoxW(hwnd_, error.c_str(), L"Mezozoy", MB_ICONERROR);
        return false;
    }
    projectActive_ = true;
    home_->setProjectActive(true);
    settings_.edit().lastProject = path.wstring();
    settings_.save();
    if (recovered) {
        setStatus(L"Восстановлена копия: " + selectedPath.filename().wstring() + L". Нажмите Ctrl+S, чтобы сохранить ее в проект.");
    } else {
        setStatus(L"Открыт: " + path.filename().wstring());
    }
    switchTo(View::Script);
    return true;
}

bool MainWindow::saveProject(bool saveAs) {
    if (current_) current_->commit(); std::filesystem::path file = document_.filePath();
    if (saveAs || file.empty()) {
        const std::wstring suggested = utf::FileNameSafe(document_.project().title) + L".mzoy";
        file = OpenFileDialog(hwnd_, true, L"Проект Mezozoy (*.mzoy)\0*.mzoy\0Все файлы (*.*)\0*.*\0\0", L"mzoy", suggested); if (file.empty()) return false;
    }
    std::wstring error; const bool ok = saveAs || document_.filePath().empty() ? document_.saveAs(file, &error) : document_.save(&error);
    if (!ok) { MessageBoxW(hwnd_, error.c_str(), L"Mezozoy", MB_ICONERROR); return false; }
    settings_.edit().lastProject = file.wstring(); settings_.save(); setStatus(L"Проект сохранен"); updateTitle(); return true;
}

void MainWindow::showPreview() {
    if (current_) current_->commit();
    printer_.showPreview(hwnd_, document_.project(),
                         ScreenplayLayoutService::paperFromSetting(settings_.get().screenplayPaper));
}

void MainWindow::importScript() {
    if (current_) current_->commit();
    const auto file = OpenFileDialog(hwnd_, false, L"Сценарии Fountain и TXT (*.fountain;*.txt)\0*.fountain;*.txt\0Fountain (*.fountain)\0*.fountain\0Текст (*.txt)\0*.txt\0\0", L"fountain");
    if (file.empty()) return;
    const bool fountain = _wcsicmp(file.extension().c_str(), L".fountain") == 0;
    ImportPreview preview = exchange_.preview(file, fountain ? ScriptExchangeFormat::Fountain : ScriptExchangeFormat::Text);
    if (!preview.error.empty()) { MessageBoxW(hwnd_, preview.error.c_str(), L"Импорт", MB_ICONERROR); return; }
    std::wstring message = L"Найдено сцен: " + std::to_wstring(preview.scenes.size()) + L"\nПерсонажей: " + std::to_wstring(preview.detectedCharacters.size()) +
                           L"\nЛокаций: " + std::to_wstring(preview.detectedLocations.size()) + L"\n\nДобавить их в текущий проект?";
    if (MessageBoxW(hwnd_, message.c_str(), L"Предпросмотр импорта", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    document_.checkpoint(L"Импорт сценария");
    const std::size_t count = exchange_.apply(document_.editProject(), std::move(preview), true);
    if (count) { document_.markChanged(); document_.setSelectedScene(document_.project().scenes.size() - count); setStatus(L"Импортировано сцен: " + std::to_wstring(count)); switchTo(View::Script); }
}

void MainWindow::exportScript() {
    if (current_) current_->commit(); const auto file = OpenFileDialog(hwnd_, true, L"Fountain (*.fountain)\0*.fountain\0Текст (*.txt)\0*.txt\0\0", L"fountain", utf::FileNameSafe(document_.project().title) + L".fountain");
    if (file.empty()) return; const bool fountain = _wcsicmp(file.extension().c_str(), L".fountain") == 0; std::wstring error;
    if (!exchange_.exportText(file, document_.project(), fountain, &error)) MessageBoxW(hwnd_, error.c_str(), L"Экспорт", MB_ICONERROR); else setStatus(L"Сценарий экспортирован: " + file.filename().wstring());
}

void MainWindow::projectSearch() {
    if (current_) current_->commit(); const std::wstring query = PromptText(hwnd_, L"Поиск по проекту", L"Сцены, персонажи, локации и мир", L"", theme_); if (query.empty()) return;
    const auto results = search_.search(document_.project(), query, 30); if (results.empty()) { MessageBoxW(hwnd_, L"Ничего не найдено.", L"Поиск", MB_ICONINFORMATION); return; }
    HMENU menu = CreatePopupMenu(); for (std::size_t index = 0; index < results.size(); ++index) {
        std::wstring caption = results[index].category + L": " + results[index].title; if (!results[index].preview.empty()) caption += L" — " + results[index].preview;
        AppendMenuW(menu, MF_STRING, 8700 + static_cast<UINT>(index), caption.c_str());
    }
    RECT rect{}; GetWindowRect(hwnd_, &rect); const int command = TrackPopupMenu(menu, TPM_RETURNCMD, rect.left + (rect.right - rect.left) / 3, rect.top + 90, 0, hwnd_, nullptr); DestroyMenu(menu);
    if (command < 8700 || command >= 8700 + static_cast<int>(results.size())) return; const auto& selected = results[static_cast<std::size_t>(command - 8700)];
    if (selected.sceneId || selected.kind == SearchResultKind::Scene) { document_.setSelectedSceneById(selected.sceneId ? selected.sceneId : selected.itemId); switchTo(View::Script); }
    else switchTo(View::Development);
}

void MainWindow::commandPalette() {
    HMENU menu = CreatePopupMenu();
    const std::pair<int, const wchar_t*> commands[] = {{IdScript, L"Открыть сценарий"}, {IdCards, L"Открыть карточки"}, {IdDevelopment, L"Открыть разработку"}, {IdImport, L"Импорт Fountain / TXT"}, {IdExport, L"Экспорт Fountain / TXT"}, {IdProjectSearch, L"Поиск по проекту"}, {IdSettings, L"Открыть настройки"}};
    for (const auto& command : commands) AppendMenuW(menu, MF_STRING, command.first, command.second); RECT rect{}; GetWindowRect(hwnd_, &rect);
    const int selected = TrackPopupMenu(menu, TPM_RETURNCMD, rect.left + (rect.right - rect.left) / 2 - 120, rect.top + 80, 0, hwnd_, nullptr); DestroyMenu(menu); if (selected) SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(selected, BN_CLICKED), 0);
}

void MainWindow::applySettings() {
    theme_ = ThemeFromSettings(settings_.get());

    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    if (navigationBrush_) DeleteObject(navigationBrush_);
    if (panelBrush_) DeleteObject(panelBrush_);
    if (updateBrush_) DeleteObject(updateBrush_);
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    navigationBrush_ = CreateSolidBrush(theme_.navigation);
    panelBrush_ = CreateSolidBrush(theme_.panel);
    updateBrush_ = CreateSolidBrush(theme_.panelAlt);

    if (uiFont_) DeleteObject(uiFont_);
    if (navFont_) DeleteObject(navFont_);
    if (brandFont_) DeleteObject(brandFont_);
    const float scale = settings_.get().uiScale;
    uiFont_ = CreateFontW(-std::max(12, static_cast<int>(std::lround(16.0f * scale))), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    navFont_ = CreateFontW(-std::max(12, static_cast<int>(std::lround(15.0f * scale))), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    brandFont_ = CreateFontW(-std::max(13, static_cast<int>(std::lround(17.0f * scale))), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    SetFont(status_, uiFont_);
    SetFont(projectLabel_, uiFont_);
    SetFont(brand_, brandFont_);
    SetFont(updateLabel_, uiFont_);
    SetFont(updateDownload_, uiFont_);
    SetFont(updateLater_, uiFont_);
    for (HWND button : navButtons_) SetFont(button, navFont_);
    for (HWND button : topButtons_) SetFont(button, uiFont_);

    for (Page* page : {static_cast<Page*>(home_.get()), static_cast<Page*>(development_.get()),
                       static_cast<Page*>(cards_.get()), static_cast<Page*>(script_.get()), static_cast<Page*>(settingsPage_.get())})
        page->applyAppearance(theme_, scale);

    KillTimer(hwnd_, AutosaveTimer);
    SetTimer(hwnd_, AutosaveTimer, static_cast<UINT>(settings_.get().autosaveSeconds * 1000), nullptr);
    document_.setHistoryLimit(static_cast<std::size_t>(settings_.get().historySteps));
    document_.setBackupPolicy(settings_.get().backups, static_cast<std::size_t>(settings_.get().maxBackups));
    script_->refreshFromDocument();
    layout(true, true);
    // Static shell backgrounds retain their pixels when only the parent is invalidated.
    for (HWND surface : {navigation_, topBar_, content_, brand_, projectLabel_, status_, updateBanner_, updateLabel_})
        InvalidateRect(surface, nullptr, TRUE);
    for (HWND button : navButtons_) InvalidateRect(button, nullptr, TRUE);
    for (HWND button : topButtons_) InvalidateRect(button, nullptr, TRUE);
    InvalidateRect(hwnd_, nullptr, TRUE);
    setStatus(L"Настройки сохранены и применены");
}

void MainWindow::checkForUpdates() {
    std::wstring repository = UpdateRepository;
    wchar_t testRepository[256]{};
    const DWORD testRepositorySize = GetEnvironmentVariableW(L"MEZOZOY_UPDATE_TEST_REPOSITORY", testRepository,
                                                              static_cast<DWORD>(std::size(testRepository)));
    if (testRepositorySize > 0 && testRepositorySize < std::size(testRepository)) repository = testRepository;
    if (repository.empty() || updateThread_.joinable()) return;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    constexpr std::int64_t CheckIntervalSeconds = 6 * 60 * 60;
    if (settings_.get().lastUpdateCheckEpoch > 0 &&
        static_cast<std::int64_t>(now) - settings_.get().lastUpdateCheckEpoch < CheckIntervalSeconds) return;
    settings_.edit().lastUpdateCheckEpoch = static_cast<std::int64_t>(now);
    settings_.save();
    const HWND target = hwnd_;
    const std::wstring currentVersion = AppVersion;
    updateThread_ = std::thread([target, repository, currentVersion] {
        auto info = UpdateService::check(repository, currentVersion);
        if (!info) return;
        auto* message = new UpdateInfo(std::move(*info));
        if (!PostMessageW(target, UpdateResultMessage, 0, reinterpret_cast<LPARAM>(message))) delete message;
    });
}

void MainWindow::showUpdate(UpdateInfo info) {
    updateUrl_ = std::move(info.releaseUrl);
    std::wstring version = std::move(info.version);
    if (!version.empty() && (version.front() == L'v' || version.front() == L'V')) version.erase(version.begin());
    SetWindowTextW(updateLabel_, (L"Доступен Mezozoy " + version + L". Скачайте новую версию с GitHub.").c_str());
    updateVisible_ = true;
    layout(true, true);
    RedrawWindow(hwnd_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void MainWindow::hideUpdate() {
    updateVisible_ = false;
    layout(true, true);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::updateTitle() {
    std::wstring title = document_.project().title + (document_.dirty() ? L" *" : L"") + L" — " + AppNameAndVersion + L" [C++]"; SetWindowTextW(hwnd_, title.c_str());
}

void MainWindow::updateShellText() {
    const std::wstring project = document_.project().title.empty() ? L"Без названия" : document_.project().title;
    SetWindowTextW(projectLabel_, (L"Проект: " + project + (document_.dirty() ? L"  •  есть изменения" : L"")).c_str());
    InvalidateRect(projectLabel_, nullptr, TRUE);
}

void MainWindow::registerFileAssociation() {
    wchar_t executable[MAX_PATH]{}; GetModuleFileNameW(nullptr, executable, MAX_PATH);
    auto set = [](HKEY root, const std::wstring& key, const std::wstring& value) { HKEY handle{}; if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) == ERROR_SUCCESS) { RegSetValueExW(handle, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))); RegCloseKey(handle); } };
    set(HKEY_CURRENT_USER, L"Software\\Classes\\.mzoy", L"Mezozoy.Project"); set(HKEY_CURRENT_USER, L"Software\\Classes\\Mezozoy.Project", L"Проект Mezozoy");
    set(HKEY_CURRENT_USER, L"Software\\Classes\\Mezozoy.Project\\DefaultIcon", std::wstring(executable) + L",0");
    set(HKEY_CURRENT_USER, L"Software\\Classes\\Mezozoy.Project\\shell\\open\\command", L"\"" + std::wstring(executable) + L"\" \"%1\"");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void MainWindow::paintNavigation(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON) return;
    const bool selected = navigationId(view_) == static_cast<int>(draw->CtlID);
    const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
    const bool hot=GetPropW(draw->hwndItem,style::Hot)!=nullptr;
    const COLORREF fill = selected ? theme_.selection : (pressed || hot ? theme_.panelAlt : theme_.navigation);
    style::Fill(draw->hDC,draw->rcItem,theme_.navigation);
    RECT surface=draw->rcItem; InflateRect(&surface,-2,-2);
    style::Surface(draw->hDC,surface,fill,selected?Theme::Blend(theme_.navigation,theme_.accent,35):fill);
    if (selected) {
        RECT bar = surface;
        bar.left += 3; bar.right = bar.left + 3;
        bar.top += 9; bar.bottom -= 9;
        style::Surface(draw->hDC, bar, theme_.accent, theme_.accent, 1);
    }
    RECT iconBounds = draw->rcItem;
    iconBounds.left += 14;
    iconBounds.right = iconBounds.left + 22;
    iconBounds.top += (iconBounds.bottom - iconBounds.top - 22) / 2;
    iconBounds.bottom = iconBounds.top + 22;
    drawNavigationIcon(draw->hDC, static_cast<int>(draw->CtlID), iconBounds, selected ? theme_.accent : theme_.textMuted);

    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, selected ? theme_.text : theme_.textMuted);
    SelectObject(draw->hDC, navFont_);
    std::wstring text = WindowText(draw->hwndItem);
    RECT textBounds = draw->rcItem;
    textBounds.left += 50;
    textBounds.right -= 10;
    DrawTextW(draw->hDC, text.c_str(), -1, &textBounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void MainWindow::drawNavigationIcon(HDC dc, int commandId, const RECT& bounds, COLORREF color) {
    style::Icon icon=style::Icon::Document;
    switch (commandId) {
        case IdHome: icon=style::Icon::Home; break;
        case IdDevelopment: icon=style::Icon::Development; break;
        case IdCards: icon=style::Icon::Cards; break;
        case IdSettings: icon=style::Icon::Settings; break;
    }
    style::DrawIcon(dc,icon,bounds,color);
}

void MainWindow::paintTopButton(DRAWITEMSTRUCT* draw) {
    if (!draw || draw->CtlType != ODT_BUTTON) return;
    style::Button(draw,theme_,theme_.panel);
    const auto icon=draw->CtlID==IdUndo?style::Icon::Undo:draw->CtlID==IdRedo?style::Icon::Redo:
                    draw->CtlID==IdSave?style::Icon::Save:style::Icon::Search;
    style::DrawIcon(draw->hDC,icon,style::IconBounds(draw->rcItem),theme_.textMuted);
}

void MainWindow::addTooltip(HWND control, const wchar_t* text) {
    if (!tooltip_) {
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, instance_, nullptr);
        SetWindowPos(tooltip_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    TOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = hwnd_;
    info.uId = reinterpret_cast<UINT_PTR>(control);
    info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

bool MainWindow::globalShortcut(WPARAM key) {
    // Keep physical modifier state from the message queue as well as Win32's
    // instantaneous state. This makes rapid shortcut sequences reliable even
    // when the key is released before a busy frame processes its WM_KEYDOWN.
    const bool ctrl = controlKeyDown_ || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = shiftKeyDown_ || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool textInput = IsTextInput(GetFocus());
    if (key == VK_F12) { SendMessageW(hwnd_, WM_COMMAND, IdPreview, 0); return true; }
    if (ctrl && key == 'S') { SendMessageW(hwnd_, WM_COMMAND, shift ? IdSaveAs : IdSave, 0); return true; }
    if (ctrl && key == 'O') { openProject(); return true; }
    if (ctrl && key == 'N') { newProject(); return true; }
    if (ctrl && shift && key == 'F') { projectSearch(); return true; }
    if (ctrl && key == 'K') { commandPalette(); return true; }
    if (ctrl && key == 'I') { importScript(); return true; }
    if (ctrl && shift && key == 'E') { exportScript(); return true; }
    if (!textInput && ctrl && key == 'C') return copySelection();
    if (!textInput && ctrl && key == 'V') return pasteSelection();
    if (!textInput && ctrl && !shift && key == 'Z') return undoProject();
    if (!textInput && ctrl && (key == 'Y' || (shift && key == 'Z'))) return redoProject();
    // Text-editing shortcuts are deliberately left to native text controls.
    return false;
}

bool MainWindow::copySelection() {
    bool copied = false;
    if (view_ == View::Cards) copied = cards_->copySelection(clipboard_);
    else if (view_ == View::Development) copied = development_->copySelection(clipboard_);
    else if (view_ == View::Script) {
        if (const Scene* scene = document_.selectedScenePtr()) {
            clipboard_.clear(); clipboard_.kind = ProjectClipboardKind::Scenes; clipboard_.scenes.push_back(*scene); copied = true;
        }
    }
    if (copied) setStatus(L"Объект скопирован");
    return copied;
}

bool MainWindow::pasteSelection() {
    if (clipboard_.empty()) return false;
    bool pasted = false;
    if (view_ == View::Cards) pasted = cards_->pasteSelection(clipboard_);
    else if (view_ == View::Development) pasted = development_->pasteSelection(clipboard_);
    else if (view_ == View::Script && !clipboard_.scenes.empty()) {
        document_.checkpoint(L"Вставка сцены");
        Project& project = document_.editProject();
        std::size_t insertion = std::min(document_.selectedScene() + 1, project.scenes.size());
        const std::size_t firstInserted = insertion;
        for (const Scene& scene : clipboard_.scenes) {
            Scene copy = CloneSceneForProject(project, scene);
            project.scenes.insert(project.scenes.begin() + static_cast<std::ptrdiff_t>(insertion++), std::move(copy));
        }
        project.normalizeOrder(); document_.markChanged(); document_.setSelectedScene(firstInserted); pasted = true;
    }
    if (pasted) {
        refreshCurrentView();
        updateShellText();
        updateTitle();
        setStatus(L"Объект вставлен");
    }
    return pasted;
}

bool MainWindow::undoProject() {
    if (view_ == View::Script && script_->undoText()) return true;
    std::wstring label;
    if (!document_.undo(&label)) { setStatus(L"Нет действий для отмены"); return true; }
    refreshCurrentView();
    updateShellText();
    updateTitle();
    setStatus(L"Отменено: " + label); return true;
}

bool MainWindow::redoProject() {
    if (view_ == View::Script && script_->undoText(true)) return true;
    std::wstring label;
    if (!document_.redo(&label)) { setStatus(L"Нет действий для повтора"); return true; }
    refreshCurrentView();
    updateShellText();
    updateTitle();
    setStatus(L"Повторено: " + label); return true;
}

void MainWindow::refreshCurrentView() {
    switch (view_) {
        case View::Home:
            home_->setProjectActive(projectActive_);
            home_->refreshRecent();
            break;
        case View::Development: development_->refreshTree(); break;
        case View::Cards: cards_->refresh(); break;
        case View::Script: script_->refreshFromDocument(); break;
        case View::Settings: break;
    }
}

int MainWindow::navigationId(View view) const {
    switch (view) {
        case View::Home: return IdHome;
        case View::Development: return IdDevelopment;
        case View::Cards: return IdCards;
        case View::Script: return IdScript;
        case View::Settings: return IdSettings;
    }
    return 0;
}

}  // namespace mezozoy::ui
