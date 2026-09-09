#include "SettingsService.h"

#include "../core/Utf.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace mezozoy {
namespace {

std::filesystem::path Folder(int csidl) {
    wchar_t buffer[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, buffer))) return buffer;
    return std::filesystem::current_path();
}

std::map<std::string, std::string> ParseObject(const std::string& json) {
    std::map<std::string, std::string> result;
    std::size_t p = 0;
    auto skip = [&]() { while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p; };
    skip();
    if (p < json.size() && json[p] == '{') ++p;
    while (p < json.size()) {
        skip();
        if (p >= json.size() || json[p] == '}') break;
        if (json[p] != '"') { ++p; continue; }
        ++p;
        std::string key;
        while (p < json.size() && json[p] != '"') {
            if (json[p] == '\\' && p + 1 < json.size()) key.push_back(json[++p]); else key.push_back(json[p]);
            ++p;
        }
        if (p < json.size()) ++p;
        skip(); if (p < json.size() && json[p] == ':') ++p; skip();
        std::string value;
        if (p < json.size() && json[p] == '"') {
            ++p;
            while (p < json.size() && json[p] != '"') {
                if (json[p] == '\\' && p + 1 < json.size()) {
                    const char next = json[++p];
                    if (next == 'n') value.push_back('\n'); else if (next == 'r') value.push_back('\r'); else value.push_back(next);
                } else value.push_back(json[p]);
                ++p;
            }
            if (p < json.size()) ++p;
        } else {
            const std::size_t begin = p;
            while (p < json.size() && json[p] != ',' && json[p] != '}') ++p;
            value = json.substr(begin, p - begin);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        }
        result[key] = value;
        while (p < json.size() && json[p] != ',' && json[p] != '}') ++p;
        if (p < json.size() && json[p] == ',') ++p;
    }
    return result;
}

std::string Escape(const std::wstring& value) {
    std::string text = utf::ToUtf8(value);
    std::string result;
    result.reserve(text.size() + 8);
    for (const char c : text) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result.push_back(c);
    }
    return result;
}

bool Bool(const std::map<std::string, std::string>& values, const char* key, bool fallback) {
    const auto found = values.find(key);
    if (found == values.end()) return fallback;
    return found->second == "true" || found->second == "1";
}

int Int(const std::map<std::string, std::string>& values, const char* key, int fallback) {
    const auto found = values.find(key); if (found == values.end()) return fallback;
    try { return std::stoi(found->second); } catch (...) { return fallback; }
}

std::int64_t Int64(const std::map<std::string, std::string>& values, const char* key, std::int64_t fallback) {
    const auto found = values.find(key); if (found == values.end()) return fallback;
    try { return std::stoll(found->second); } catch (...) { return fallback; }
}

float Float(const std::map<std::string, std::string>& values, const char* key, float fallback) {
    const auto found = values.find(key); if (found == values.end()) return fallback;
    try { return std::stof(found->second); } catch (...) { return fallback; }
}

std::wstring String(const std::map<std::string, std::string>& values, const char* key, const std::wstring& fallback) {
    const auto found = values.find(key); return found == values.end() ? fallback : utf::FromUtf8(found->second);
}

}  // namespace

SettingsService::SettingsService() {
    settings_.projectsFolder = DefaultProjectsFolder().wstring();
}

std::filesystem::path SettingsService::DefaultProjectsFolder() {
    return Folder(CSIDL_PERSONAL) / L"Mezozoy Projects";
}

std::filesystem::path SettingsService::path() const {
    wchar_t profile[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"MEZOZOY_PROFILE_DIR", profile, 32768);
    if (size > 0 && size < 32768 && std::filesystem::path(profile).is_absolute())
        return std::filesystem::path(profile) / L"settings.json";
    return Folder(CSIDL_APPDATA) / L"Mezozoy" / L"settings.json";
}

bool SettingsService::load() {
    try {
        if (!std::filesystem::exists(path())) { save(); return true; }
        const auto values = ParseObject(utf::ReadFile(path()));
        settings_.themeMode = String(values, "ThemeMode", settings_.themeMode);
        settings_.accentColorName = String(values, "AccentColorName", settings_.accentColorName);
        settings_.accentColorHex = String(values, "AccentColorHex", settings_.accentColorHex);
        settings_.uiScale = Float(values, "UiScale", settings_.uiScale);
        settings_.fontSizeOffset = Int(values, "FontSizeOffset", settings_.fontSizeOffset);
        settings_.compactMode = Bool(values, "UseCompactMode", settings_.compactMode);
        settings_.diagnostics = Bool(values, "ShowDiagnosticsOverlay", settings_.diagnostics);
        settings_.autosave = Bool(values, "AutosaveEnabled", settings_.autosave);
        settings_.autosaveSeconds = Int(values, "AutosaveIntervalSeconds", settings_.autosaveSeconds);
        settings_.backups = Bool(values, "BackupsEnabled", settings_.backups);
        settings_.maxBackups = Int(values, "MaxBackupCount", settings_.maxBackups);
        settings_.projectsFolder = String(values, "DefaultProjectsFolder", settings_.projectsFolder);
        settings_.lastProject = String(values, "LastOpenedProjectPath", settings_.lastProject);
        settings_.scriptFont = String(values, "ScriptFontFamily", settings_.scriptFont);
        settings_.scriptFontSize = Float(values, "ScriptFontSize", settings_.scriptFontSize);
        settings_.screenplayPaper = String(values, "ScreenplayPaper", settings_.screenplayPaper);
        settings_.pageView = Bool(values, "ScriptUsePageView", settings_.pageView);
        settings_.lineTypeIndicator = Bool(values, "ScriptShowLineTypeIndicator", settings_.lineTypeIndicator);
        settings_.autocomplete = Bool(values, "ScriptEnableAutocomplete", settings_.autocomplete);
        settings_.smartEnter = Bool(values, "ScriptSmartEnter", settings_.smartEnter);
        settings_.showCounters = Bool(values, "ScriptShowCounters", settings_.showCounters);
        settings_.confirmDeleteScene = Bool(values, "ConfirmBeforeDeletingScene", settings_.confirmDeleteScene);
        settings_.confirmDeleteCard = Bool(values, "ConfirmBeforeDeletingCard", settings_.confirmDeleteCard);
        settings_.selectMatchingCard = Bool(values, "SelectMatchingCard", settings_.selectMatchingCard);
        settings_.selectMatchingScene = Bool(values, "SelectMatchingScene", settings_.selectMatchingScene);
        settings_.keepScriptCardsSync = Bool(values, "KeepScriptCardsSync", settings_.keepScriptCardsSync);
        settings_.askSave = Bool(values, "AskToSaveBeforeClosing", settings_.askSave);
        settings_.restoreLastProject = Bool(values, "RestoreLastProjectOnStartup", settings_.restoreLastProject);
        settings_.historySteps = Int(values, "ProjectHistorySteps", settings_.historySteps);
        settings_.lastUpdateCheckEpoch = Int64(values, "LastUpdateCheckEpoch", settings_.lastUpdateCheckEpoch);
        normalize();
        return true;
    } catch (...) {
        reset();
        return false;
    }
}

void SettingsService::normalize() {
    if (settings_.themeMode != L"Dark" && settings_.themeMode != L"Darker" && settings_.themeMode != L"Light")
        settings_.themeMode = L"Dark";
    settings_.uiScale = std::clamp(settings_.uiScale, 0.8f, 1.5f);
    settings_.fontSizeOffset = std::clamp(settings_.fontSizeOffset, -4, 8);
    settings_.autosaveSeconds = std::clamp(settings_.autosaveSeconds, 15, 600);
    settings_.maxBackups = std::clamp(settings_.maxBackups, 1, 100);
    settings_.scriptFont = L"Courier New";
    settings_.scriptFontSize = 12.0f;
    settings_.historySteps = std::clamp(settings_.historySteps, 0, 1000);
    settings_.lastUpdateCheckEpoch = std::max<std::int64_t>(0, settings_.lastUpdateCheckEpoch);
    if (settings_.projectsFolder.empty()) settings_.projectsFolder = DefaultProjectsFolder().wstring();
    settings_.screenplayPaper = utf::ToUpper(utf::Trim(settings_.screenplayPaper)) == L"A4"
        ? L"A4" : L"HollywoodLetter";
}

bool SettingsService::save(std::wstring* error) const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n";
    auto s = [&](const char* key, const std::wstring& value) { out << "  \"" << key << "\": \"" << Escape(value) << "\",\n"; };
    auto b = [&](const char* key, bool value) { out << "  \"" << key << "\": " << (value ? "true" : "false") << ",\n"; };
    auto i = [&](const char* key, int value) { out << "  \"" << key << "\": " << value << ",\n"; };
    s("ThemeMode", settings_.themeMode); s("AccentColorName", settings_.accentColorName); s("AccentColorHex", settings_.accentColorHex);
    out << "  \"UiScale\": " << settings_.uiScale << ",\n";
    i("FontSizeOffset", settings_.fontSizeOffset);
    b("UseCompactMode", settings_.compactMode); b("ShowDiagnosticsOverlay", settings_.diagnostics); b("AutosaveEnabled", settings_.autosave);
    i("AutosaveIntervalSeconds", settings_.autosaveSeconds); b("BackupsEnabled", settings_.backups); i("MaxBackupCount", settings_.maxBackups);
    s("DefaultProjectsFolder", settings_.projectsFolder); s("LastOpenedProjectPath", settings_.lastProject); s("ScriptFontFamily", settings_.scriptFont);
    out << "  \"ScriptFontSize\": " << settings_.scriptFontSize << ",\n";
    s("ScreenplayPaper", settings_.screenplayPaper);
    b("ScriptUsePageView", settings_.pageView); b("ScriptShowLineTypeIndicator", settings_.lineTypeIndicator);
    b("ScriptEnableAutocomplete", settings_.autocomplete); b("ScriptSmartEnter", settings_.smartEnter);
    b("ScriptShowCounters", settings_.showCounters);
    b("ConfirmBeforeDeletingScene", settings_.confirmDeleteScene); b("ConfirmBeforeDeletingCard", settings_.confirmDeleteCard);
    b("SelectMatchingCard", settings_.selectMatchingCard); b("SelectMatchingScene", settings_.selectMatchingScene);
    b("KeepScriptCardsSync", settings_.keepScriptCardsSync);
    b("AskToSaveBeforeClosing", settings_.askSave);
    b("RestoreLastProjectOnStartup", settings_.restoreLastProject);
    i("ProjectHistorySteps", settings_.historySteps);
    out << "  \"LastUpdateCheckEpoch\": " << settings_.lastUpdateCheckEpoch << "\n}";
    return utf::WriteFileAtomic(path(), out.str(), error);
}

void SettingsService::reset() {
    settings_ = AppSettings{};
    settings_.projectsFolder = DefaultProjectsFolder().wstring();
    save();
}

}  // namespace mezozoy
