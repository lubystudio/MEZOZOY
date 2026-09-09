#pragma once

#include <filesystem>
#include <cstdint>
#include <string>

namespace mezozoy {

struct AppSettings {
    std::wstring themeMode = L"Dark";
    std::wstring accentColorName = L"Mezozoy Purple";
    std::wstring accentColorHex = L"#8B5CF6";
    float uiScale = 1.0f;
    int fontSizeOffset = 0;
    bool compactMode = false;
    bool diagnostics = false;
    bool autosave = true;
    int autosaveSeconds = 60;
    bool backups = true;
    int maxBackups = 20;
    std::wstring projectsFolder;
    std::wstring lastProject;
    std::wstring scriptFont = L"Courier New";
    float scriptFontSize = 12.0f;
    std::wstring screenplayPaper = L"HollywoodLetter";
    bool pageView = true;
    bool lineTypeIndicator = true;
    bool autocomplete = true;
    bool smartEnter = true;
    bool showCounters = true;
    bool confirmDeleteScene = true;
    bool confirmDeleteCard = true;
    bool selectMatchingCard = true;
    bool selectMatchingScene = true;
    bool keepScriptCardsSync = true;
    bool askSave = true;
    bool restoreLastProject = false;
    int historySteps = 100;
    std::int64_t lastUpdateCheckEpoch = 0;
};

class SettingsService {
public:
    SettingsService();

    const AppSettings& get() const { return settings_; }
    AppSettings& edit() { return settings_; }
    std::filesystem::path path() const;
    bool load();
    bool save(std::wstring* error = nullptr) const;
    void reset();

    static std::filesystem::path DefaultProjectsFolder();

private:
    AppSettings settings_;
    void normalize();
};

}  // namespace mezozoy
