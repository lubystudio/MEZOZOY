#pragma once

#include "CardsPage.h"
#include "DevelopmentPage.h"
#include "HomePage.h"
#include "ScriptPage.h"
#include "SettingsPage.h"
#include "Theme.h"
#include "WorkspaceHost.h"
#include "../services/GlobalSearchService.h"
#include "../services/ImportExportService.h"
#include "../services/PrintService.h"
#include "../services/ProjectClipboard.h"
#include "../services/ProjectDocument.h"
#include "../services/RecoveryService.h"
#include "../services/SettingsService.h"
#include "../services/UpdateService.h"

#include <windows.h>

#include <memory>
#include <thread>
#include <vector>

namespace mezozoy::ui {

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();
    bool create(const std::filesystem::path& startupFile = {});
    int run(int showCommand);
    HWND hwnd() const { return hwnd_; }

private:
    enum class View { Home, Development, Cards, Script, Settings };
    enum : int {
        IdHome = 6101, IdDevelopment = 6103, IdCards = 6104, IdScript = 6105, IdSettings = 6110, IdStatus = 6111,
        IdSave = 6190, IdSaveAs, IdPreview, IdImport, IdExport, IdProjectSearch, IdCommandPalette, IdUndo, IdRedo,
        IdUpdateDownload, IdUpdateLater
    };
    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND navigation_{};
    HWND topBar_{};
    HWND content_{};
    HWND status_{};
    HWND brand_{};
    HWND projectLabel_{};
    HWND tooltip_{};
    HWND updateBanner_{};
    HWND updateLabel_{};
    HWND updateDownload_{};
    HWND updateLater_{};
    HFONT uiFont_{};
    HFONT navFont_{};
    HFONT brandFont_{};
    HBRUSH backgroundBrush_{};
    HBRUSH navigationBrush_{};
    HBRUSH panelBrush_{};
    HBRUSH updateBrush_{};
    Theme theme_;
    SettingsService settings_;
    ProjectDocument document_;
    RecoveryService recovery_;
    PrintService printer_;
    ImportExportService exchange_;
    GlobalSearchService search_;
    std::unique_ptr<HomePage> home_;
    std::unique_ptr<DevelopmentPage> development_;
    std::unique_ptr<CardsPage> cards_;
    std::unique_ptr<ScriptPage> script_;
    std::unique_ptr<SettingsPage> settingsPage_;
    WorkspaceHost workspace_;
    ProjectClipboard clipboard_;
    Page* current_{};
    View view_ = View::Home;
    std::vector<HWND> navButtons_;
    std::vector<HWND> topButtons_;
    bool closing_ = false;
    bool projectActive_ = false;
    bool controlKeyDown_ = false;
    bool shiftKeyDown_ = false;
    bool workspaceLayoutPending_ = true;
    bool updateVisible_ = false;
    std::wstring updateUrl_;
    std::thread updateThread_;
    int lastLayoutWidth_ = -1;
    int lastLayoutHeight_ = -1;
    static constexpr UINT_PTR AutosaveTimer = 6201;
    static constexpr UINT QueryAccentMessage = WM_APP + 92;
    static constexpr UINT QuerySelectionMessage = WM_APP + 93;
    static constexpr UINT UpdateResultMessage = WM_APP + 94;

    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void buildShell();
    void createPages();
    void layout(bool includeWorkspace = true, bool force = false);
    void switchTo(View view);
    void setStatus(const std::wstring& text);
    bool confirmReplace();
    void newProject();
    void openProject();
    bool openPath(const std::filesystem::path& path);
    bool saveProject(bool saveAs = false);
    void showPreview();
    void importScript();
    void exportScript();
    void projectSearch();
    void commandPalette();
    void applySettings();
    void updateTitle();
    void registerFileAssociation();
    void paintNavigation(DRAWITEMSTRUCT* draw);
    void paintTopButton(DRAWITEMSTRUCT* draw);
    void drawNavigationIcon(HDC dc, int commandId, const RECT& bounds, COLORREF color);
    void addTooltip(HWND control, const wchar_t* text);
    void updateShellText();
    void checkForUpdates();
    void showUpdate(UpdateInfo info);
    void hideUpdate();
    void refreshCurrentView();
    bool globalShortcut(WPARAM key);
    bool copySelection();
    bool pasteSelection();
    bool undoProject();
    bool redoProject();
    int navigationId(View view) const;
};

}  // namespace mezozoy::ui
