#pragma once

#include "Page.h"
#include "../services/SettingsService.h"

#include <array>
#include <functional>
#include <map>
#include <vector>

namespace mezozoy::ui {

class SettingsPage final : public NativePage {
public:
    SettingsPage(HINSTANCE instance, HWND parent, SettingsService& settings, const Theme& theme);
    void onLayout(int width, int height) override;
    bool handleCommand(int id, int code, HWND source) override;
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void applyAppearance(const Theme& theme, float uiScale) override;
    void commit() override;
    void setApplyAction(std::function<void()> action) { apply_ = std::move(action); }

protected:
    HBRUSH controlColor(HDC dc, HWND control, UINT message) override;
    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;

private:
    enum : int {
        IdCategories = 5101,
        IdThemeDark,
        IdThemeDarker,
        IdAccent,
        IdCompact,
        IdDiagnostics,
        IdFont,
        IdFontSize,
        IdPageView,
        IdLineType,
        IdAutocomplete,
        IdSmartEnter,
        IdProjectFolder,
        IdBrowse,
        IdAutosave,
        IdAutosaveSeconds,
        IdBackups,
        IdBackupCount,
        IdConfirmScene,
        IdConfirmCard,
        IdAskSave,
        IdRestore,
        IdShowCounters,
        IdSelectMatchingCard,
        IdSelectMatchingScene,
        IdKeepSync,
        // Keep the public automation id stable: GUI smoke tests and future
        // accessibility tooling use this id to locate the history setting.
        IdHistorySteps = 5128,
        IdOpenProjectFolder,
        IdOpenSettingsFile,
        IdOpenSettingsFolder,
        IdReset,
        IdThemeLight = 5133,
        IdAccentFirst = 5300,
        IdScaleFirst = 5320
    };

    SettingsService& settings_;
    HWND categories_{};
    HWND title_{};
    HWND subtitle_{};
    HWND sectionTitle_{};
    HWND sectionSubtitle_{};

    HWND themeLabel_{};
    HWND themeDark_{};
    HWND themeDarker_{};
    HWND themeLight_{};
    HWND accentLabel_{};
    HWND accentHexLabel_{};
    HWND accent_{};
    std::array<HWND, 8> accentButtons_{};
    HWND scaleLabel_{};
    std::array<HWND, 4> scaleButtons_{};
    HWND compact_{};
    HWND diagnostics_{};

    HWND font_{};
    HWND fontSize_{};
    HWND pageView_{};
    HWND lineType_{};
    HWND autocomplete_{};
    HWND smartEnter_{};
    HWND showCounters_{};
    HWND projectFolder_{};
    HWND browse_{};
    HWND autosave_{};
    HWND autosaveSeconds_{};
    HWND backups_{};
    HWND backupCount_{};
    HWND confirmScene_{};
    HWND confirmCard_{};
    HWND selectMatchingCard_{};
    HWND selectMatchingScene_{};
    HWND keepSync_{};
    HWND askSave_{};
    HWND restore_{};
    HWND historySteps_{};
    HWND openProjectFolder_{};
    HWND settingsPath_{};
    HWND openSettingsFile_{};
    HWND openSettingsFolder_{};
    HWND reset_{};

    std::map<int, std::vector<HWND>> groups_;
    std::map<HWND, bool> toggleStates_;
    std::vector<RECT> cardRects_;
    RECT noticeRect_{};
    std::function<void()> apply_;
    bool loading_ = false;
    int selectedGroup_ = 0;
    int themeSelection_ = 0;
    int accentSelection_ = 0;
    int scaleSelection_ = 1;

    HWND label(const wchar_t* text, int group, bool bold = false);
    HWND check(const wchar_t* text, int id, int group);
    HWND edit(const std::wstring& text, int id, int group, bool number = false);
    HWND ownerButton(const wchar_t* text, int id, int group);
    bool isToggle(HWND control) const;
    bool isToggleChecked(HWND control) const;
    void setToggleChecked(HWND control, bool checked);
    void addTo(int group, HWND control);
    void showGroup(int group);
    void load();
    void save();
    void browseFolder();
    void invalidateVisuals();
    void drawPage(HDC dc);
};

}  // namespace mezozoy::ui
