#include "StatisticsService.h"

#include "ScriptService.h"
#include "../core/Utf.h"

#include <map>
#include <set>
#include <sstream>

namespace mezozoy {
namespace {

std::wstring LocationFromHeading(const Scene& scene) {
    const auto lines = utf::SplitLines(scene.text);
    std::wstring value = lines.empty() ? scene.title : utf::Trim(lines.front());
    const std::wstring upper = utf::ToUpper(value);
    const bool heading = upper.starts_with(L"ИНТ.") || upper.starts_with(L"НАТ.") || upper.starts_with(L"INT.") || upper.starts_with(L"EXT.");
    if (!heading) return utf::Trim(scene.title.empty() ? value : scene.title);
    const std::size_t dot = value.find(L'.');
    if (dot != std::wstring::npos) value = utf::Trim(value.substr(dot + 1));
    std::size_t separator = value.find(L'—');
    if (separator == std::wstring::npos) separator = value.find(L'–');
    if (separator == std::wstring::npos) separator = value.find(L" - ");
    if (separator != std::wstring::npos) value = utf::Trim(value.substr(0, separator));
    return value;
}

}  // namespace

StatisticsSnapshot StatisticsService::analyze(const Project& project) const {
    StatisticsSnapshot result;
    std::size_t totalPrintLines = 0;
    std::map<std::wstring, CharacterStatistics> characters;
    std::map<std::wstring, LocationStatistics> locations;
    for (const auto& item : project.characters) characters[utf::ToUpper(utf::Trim(item.name))].name = item.name;

    ScriptService script;
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        const Scene& scene = project.scenes[index];
        const std::wstring locationName = LocationFromHeading(scene);
        if (!locationName.empty()) {
            auto& location = locations[utf::ToUpper(locationName)];
            if (location.name.empty()) location.name = locationName;
            ++location.scenes;
            location.words += ScriptService::countWords(scene.text);
            location.minutes += ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats);
        }
        SceneStatistics sceneStats;
        sceneStats.number = static_cast<int>(index + 1);
        sceneStats.title = scene.title;
        sceneStats.words = ScriptService::countWords(scene.text);
        sceneStats.minutes = ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats);
        sceneStats.reviewComments = static_cast<int>(scene.reviewComments.size());
        sceneStats.breakdownItems = static_cast<int>(scene.breakdownItems.size());
        result.breakdownItems += scene.breakdownItems.size();
        for (const auto& review : scene.reviewComments) if (!review.resolved) ++result.openReviewComments;
        if (utf::Trim(scene.text).empty() || ScriptService::countWords(scene.text) <= 2) ++result.emptyScenes;
        result.totalWords += sceneStats.words;
        result.totalCharacters += scene.text.size();
        const auto lines = utf::SplitLines(scene.text);
        result.totalLines += lines.size();
        totalPrintLines += ScriptService::estimatedPrintLines(scene.text, scene.screenplayFormats);
        if (index + 1 < project.scenes.size()) totalPrintLines += 2;

        std::set<std::wstring> inScene;
        ScriptLineType previous = ScriptLineType::Empty;
        std::wstring currentCharacter;
        for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            const auto& line = lines[lineIndex];
            // A scene's first line is its title even when the user writes it in
            // uppercase. Do not count it as a speaking character.
            const ScriptLineType type = lineIndex == 0 ? ScriptLineType::SceneHeading : script.classify(line, previous);
            if (type == ScriptLineType::Character) {
                currentCharacter = utf::ToUpper(utf::Trim(line));
                auto& stat = characters[currentCharacter];
                if (stat.name.empty()) stat.name = utf::Trim(line);
                inScene.insert(currentCharacter);
            } else if (type == ScriptLineType::Dialogue && !currentCharacter.empty()) {
                auto& stat = characters[currentCharacter];
                ++stat.dialogueLines;
                stat.spokenWords += ScriptService::countWords(line);
                ++sceneStats.dialogueLines; ++result.dialogueLines;
            } else if (type == ScriptLineType::Action || type == ScriptLineType::Shot) {
                ++sceneStats.actionLines; ++result.actionLines;
            } else if (type != ScriptLineType::Parenthetical && type != ScriptLineType::Empty) {
                currentCharacter.clear();
            }
            if (type != ScriptLineType::Empty) previous = type;
            const std::wstring upperLine = utf::ToUpper(line);
            if (upperLine.find(L"TODO:") != std::wstring::npos || upperLine.find(L"[TODO]") != std::wstring::npos || upperLine.find(L"FIXME:") != std::wstring::npos) ++result.todoCount;
        }
        std::wostringstream joined;
        bool first = true;
        for (const auto& name : inScene) {
            if (!first) joined << L", ";
            first = false;
            joined << characters[name].name;
            ++characters[name].scenes;
            if (!characters[name].firstScene) characters[name].firstScene = static_cast<int>(index + 1);
            characters[name].lastScene = static_cast<int>(index + 1);
        }
        sceneStats.characters = joined.str();
        result.scenes.push_back(std::move(sceneStats));
    }
    result.pages = static_cast<double>(totalPrintLines) / 51.0;
    result.minutes = result.pages;
    for (auto& entry : characters) if (!entry.second.name.empty()) result.characters.push_back(std::move(entry.second));
    for (auto& entry : locations) if (!entry.second.name.empty()) result.locations.push_back(std::move(entry.second));
    return result;
}

}  // namespace mezozoy
