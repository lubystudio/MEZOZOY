#include "ScriptService.h"

#include "ScreenplayLayoutService.h"
#include "ScreenplayDocumentModel.h"
#include "../core/Utf.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace mezozoy {

std::wstring ScriptService::normalizeNewlines(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\r') {
            if (i + 1 < value.size() && value[i + 1] == L'\n') ++i;
            result.push_back(L'\n');
        } else result.push_back(value[i]);
    }
    return result;
}

std::wstring ScriptService::buildFullScript(const Project& project) const {
    return ScreenplayDocumentModel::fromProject(project).text();
}

std::size_t ScriptService::findTitleLine(const std::wstring& text, const std::wstring& title, std::size_t from) {
    const std::wstring wanted = utf::Trim(title);
    if (wanted.empty()) return std::wstring::npos;
    std::size_t lineStart = from;
    while (lineStart <= text.size()) {
        const std::size_t lineEnd = text.find(L'\n', lineStart);
        const std::size_t end = lineEnd == std::wstring::npos ? text.size() : lineEnd;
        if (utf::Trim(text.substr(lineStart, end - lineStart)) == wanted) return lineStart;
        if (lineEnd == std::wstring::npos) break;
        lineStart = lineEnd + 1;
    }
    return std::wstring::npos;
}

std::vector<SceneRange> ScriptService::calculateRanges(const Project& project, const std::wstring& fullText) const {
    const std::wstring normalized = normalizeNewlines(fullText);
    const std::wstring previous = normalizeNewlines(buildFullScript(project));

    // Scene boundaries come from the last committed document, not from guesses
    // based solely on headings. A compact prefix/suffix diff then maps those
    // boundaries into the edited text. This keeps later scenes intact when the
    // user renames their headings directly in the continuous editor.
    std::vector<std::size_t> oldStarts;
    oldStarts.reserve(project.scenes.size());
    std::size_t oldCursor = 0;
    const auto sourceModel = ScreenplayDocumentModel::fromProject(project);
    for (const auto& section : sourceModel.sections()) {
        oldStarts.push_back(oldCursor);
        for (std::size_t block = section.firstBlock; block < section.firstBlock + section.blockCount; ++block)
            oldCursor += sourceModel.blocks()[block].text.size() + 1;
    }

    std::size_t prefix = 0;
    while (prefix < previous.size() && prefix < normalized.size() && previous[prefix] == normalized[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < previous.size() - prefix && suffix < normalized.size() - prefix &&
           previous[previous.size() - 1 - suffix] == normalized[normalized.size() - 1 - suffix]) ++suffix;

    const std::size_t oldChangedEnd = previous.size() - suffix;
    const std::size_t newChangedEnd = normalized.size() - suffix;
    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(normalized.size()) - static_cast<std::ptrdiff_t>(previous.size());
    std::vector<std::size_t> starts;
    starts.reserve(oldStarts.size());
    for (std::size_t i = 0; i < oldStarts.size(); ++i) {
        const std::size_t oldStart = oldStarts[i];
        std::size_t mapped = oldStart;
        if (oldStart < prefix) {
            mapped = oldStart;
        } else if (oldStart >= oldChangedEnd) {
            const std::ptrdiff_t shifted = static_cast<std::ptrdiff_t>(oldStart) + delta;
            mapped = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, shifted));
        } else if (oldStart == prefix) {
            // Replacing/deleting a heading keeps its start. A pure insertion
            // exactly at the boundary belongs before the following scene.
            mapped = oldChangedEnd == prefix ? newChangedEnd : prefix;
        } else {
            // Multiple edits may span a boundary. Prefer a still-existing title;
            // otherwise preserve the boundary proportionally inside the edit.
            const std::size_t found = findTitleLine(normalized, project.scenes[i].title,
                                                     starts.empty() ? 0 : starts.back());
            if (found != std::wstring::npos && (starts.empty() || found >= starts.back())) {
                mapped = found;
            } else {
                const double ratio = oldChangedEnd > prefix
                    ? static_cast<double>(oldStart - prefix) / static_cast<double>(oldChangedEnd - prefix)
                    : 0.0;
                mapped = prefix + static_cast<std::size_t>(ratio * static_cast<double>(newChangedEnd - prefix));
            }
        }
        mapped = std::min(mapped, normalized.size());
        if (!starts.empty()) mapped = std::max(mapped, starts.back());
        starts.push_back(mapped);
    }
    std::vector<SceneRange> ranges;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const std::size_t start = starts[i];
        const std::size_t end = i + 1 < starts.size() ? starts[i + 1] : normalized.size();
        ranges.push_back({i, start, end > start ? end - start : 0});
    }
    return ranges;
}

void ScriptService::applyFullScript(Project& project, const std::wstring& fullText) const {
    const std::wstring normalized = normalizeNewlines(fullText);
    const auto ranges = calculateRanges(project, normalized);
    for (const auto& range : ranges) {
        if (range.sceneIndex >= project.scenes.size() || range.start > normalized.size()) continue;
        const std::size_t length = std::min(range.length, normalized.size() - range.start);
        std::wstring sceneText = normalized.substr(range.start, length);
        if (range.sceneIndex + 1 < project.scenes.size() && !sceneText.empty() && sceneText.back() == L'\n')
            sceneText.pop_back();
        Scene& scene = project.scenes[range.sceneIndex];
        // Keep the original newline representation for untouched scenes. Apart
        // from avoiding noisy project changes, this lets the document layer
        // preserve their rich-text payload while a different scene is edited
        // in the continuous script view.
        if (normalizeNewlines(scene.text) != sceneText) scene.text = sceneText;
        auto lines = utf::SplitLines(sceneText);
        const std::wstring firstLine = lines.empty() ? std::wstring{} : utf::Trim(lines.front());
        if (!firstLine.empty() && scene.title != firstLine) {
            scene.title = firstLine;
            scene.titleAutoGenerated = false;
        }
    }
    project.normalizeOrder();
}

bool ScriptService::renameScene(Project& project, std::size_t sceneIndex, const std::wstring& rawTitle) const {
    if (sceneIndex >= project.scenes.size()) return false;
    const std::wstring title = utf::Trim(rawTitle);
    Scene& scene = project.scenes[sceneIndex];
    auto lines = utf::SplitLines(scene.text);
    if (lines.empty()) lines.push_back(title); else lines.front() = title;
    scene.title = title;
    scene.text = utf::JoinLines(lines);
    scene.titleAutoGenerated = false;
    return true;
}

ScriptLineType ScriptService::classify(const std::wstring& rawLine, ScriptLineType previous) const {
    const std::wstring line = utf::Trim(rawLine);
    if (line.empty()) return ScriptLineType::Empty;
    const std::wstring upper = utf::ToUpper(line);
    if (upper.starts_with(L"ИНТ.") || upper.starts_with(L"НАТ.") || upper.starts_with(L"INT.") || upper.starts_with(L"EXT."))
        return ScriptLineType::SceneHeading;
    if (line.front() == L'(' && line.back() == L')') return ScriptLineType::Parenthetical;
    if (upper.find(L"ПЕРЕХОД") != std::wstring::npos || upper.find(L"CUT TO") != std::wstring::npos || upper.find(L"FADE TO") != std::wstring::npos ||
        (upper == line && !line.empty() && line.back() == L':')) return ScriptLineType::Transition;
    bool hasLetter = false;
    const bool allUpper = utf::ToUpper(line) == line;
    for (const wchar_t c : line) {
        if (std::iswalpha(c)) hasLetter = true;
    }
    if (hasLetter && allUpper && line.size() <= 48 && line.find(L'.') == std::wstring::npos && line.back() != L':') return ScriptLineType::Character;
    if (previous == ScriptLineType::Character || previous == ScriptLineType::Parenthetical || previous == ScriptLineType::Dialogue) return ScriptLineType::Dialogue;
    return ScriptLineType::Action;
}

wchar_t ScriptService::formatCode(ScriptLineType type) {
    switch (type) {
        case ScriptLineType::SceneHeading: return L'H';
        case ScriptLineType::Participants: return L'U';
        case ScriptLineType::Action: return L'A';
        case ScriptLineType::Character: return L'C';
        case ScriptLineType::Parenthetical: return L'P';
        case ScriptLineType::Dialogue: return L'D';
        case ScriptLineType::Transition: return L'T';
        case ScriptLineType::Note: return L'N';
        case ScriptLineType::Shot: return L'S';
        case ScriptLineType::Lyrics: return L'L';
        default: return L'E';
    }
}

ScriptLineType ScriptService::typeFromFormatCode(wchar_t code) {
    switch (code) {
        case L'H': return ScriptLineType::SceneHeading;
        case L'U': return ScriptLineType::Participants;
        case L'A': return ScriptLineType::Action;
        case L'C': return ScriptLineType::Character;
        case L'P': return ScriptLineType::Parenthetical;
        case L'D': return ScriptLineType::Dialogue;
        case L'T': return ScriptLineType::Transition;
        case L'N': return ScriptLineType::Note;
        case L'S': return ScriptLineType::Shot;
        case L'L': return ScriptLineType::Lyrics;
        default: return ScriptLineType::Empty;
    }
}

std::wstring ScriptService::buildFormatMap(const std::wstring& sceneText) const {
    const auto lines = utf::SplitLines(normalizeNewlines(sceneText));
    std::wstring result;
    result.reserve(lines.size());
    ScriptLineType previous = ScriptLineType::Empty;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        ScriptLineType type = index == 0 ? ScriptLineType::SceneHeading : classify(lines[index], previous);
        result.push_back(formatCode(type));
        previous = type;
    }
    return result;
}

std::wstring ScriptService::typeName(ScriptLineType type) {
    switch (type) {
        case ScriptLineType::SceneHeading: return L"Заголовок сцены";
        case ScriptLineType::Participants: return L"Описание действия";
        case ScriptLineType::Action: return L"Описание действия";
        case ScriptLineType::Character: return L"Персонаж";
        case ScriptLineType::Parenthetical: return L"Ремарка";
        case ScriptLineType::Dialogue: return L"Реплика";
        case ScriptLineType::Transition: return L"Переход";
        case ScriptLineType::Note: return L"Описание действия";
        case ScriptLineType::Shot: return L"Описание действия";
        case ScriptLineType::Lyrics: return L"Описание действия";
        default: return L"Пустая строка";
    }
}

ScriptLineType ScriptService::nextTypeAfterEnter(ScriptLineType type) {
    switch (type) {
        case ScriptLineType::SceneHeading: return ScriptLineType::Action;
        case ScriptLineType::Character: return ScriptLineType::Dialogue;
        case ScriptLineType::Parenthetical: return ScriptLineType::Dialogue;
        case ScriptLineType::Dialogue: return ScriptLineType::Action;
        case ScriptLineType::Transition: return ScriptLineType::SceneHeading;
        default: return ScriptLineType::Action;
    }
}

ScriptLineType ScriptService::nextTypeAfterTab(ScriptLineType type) {
    switch (type) {
        case ScriptLineType::SceneHeading: return ScriptLineType::Action;
        case ScriptLineType::Action: return ScriptLineType::Character;
        case ScriptLineType::Character: return ScriptLineType::Parenthetical;
        case ScriptLineType::Parenthetical: return ScriptLineType::Dialogue;
        case ScriptLineType::Dialogue: return ScriptLineType::Parenthetical;
        case ScriptLineType::Transition: return ScriptLineType::SceneHeading;
        case ScriptLineType::Lyrics: return ScriptLineType::Parenthetical;
        default: return ScriptLineType::Action;
    }
}

ScriptLineType ScriptService::typeForEmptyEnter(ScriptLineType type) {
    switch (type) {
        case ScriptLineType::Action: return ScriptLineType::SceneHeading;
        case ScriptLineType::Character: return ScriptLineType::Action;
        case ScriptLineType::Dialogue: return ScriptLineType::Action;
        case ScriptLineType::SceneHeading: return ScriptLineType::SceneHeading;
        case ScriptLineType::Parenthetical: return ScriptLineType::Parenthetical;
        case ScriptLineType::Transition: return ScriptLineType::Transition;
        default: return type;
    }
}

std::size_t ScriptService::countWords(const std::wstring& text) {
    std::size_t count = 0;
    bool inWord = false;
    for (const wchar_t c : text) {
        const bool word = std::iswalnum(c) != 0;
        if (word && !inWord) ++count;
        inWord = word;
    }
    return count;
}

std::size_t ScriptService::estimatedPrintLines(const std::wstring& text, const std::wstring& screenplayFormats) {
    return estimatedPrintLines(text, screenplayFormats, ScreenplayPaper::HollywoodLetter);
}

std::size_t ScriptService::estimatedPrintLines(const std::wstring& text, const std::wstring& screenplayFormats,
                                               ScreenplayPaper paper) {
    Scene scene;
    const auto lines = utf::SplitLines(normalizeNewlines(text));
    scene.title = lines.empty() || utf::Trim(lines.front()).empty() ? L"СЦЕНА" : utf::Trim(lines.front());
    scene.text = text;
    scene.screenplayFormats = screenplayFormats;
    ScreenplayLayoutService layout;
    return layout.layoutScene(scene, paper).usedLineSlots;
}

double ScriptService::estimatedMinutes(const std::wstring& text, const std::wstring& screenplayFormats) {
    return estimatedMinutes(text, screenplayFormats, ScreenplayPaper::HollywoodLetter);
}

double ScriptService::estimatedMinutes(const std::wstring& text, const std::wstring& screenplayFormats,
                                       ScreenplayPaper paper) {
    if (utf::Trim(text).empty()) return 0.0;
    Scene scene;
    const auto lines = utf::SplitLines(normalizeNewlines(text));
    scene.title = lines.empty() || utf::Trim(lines.front()).empty() ? L"СЦЕНА" : utf::Trim(lines.front());
    scene.text = text;
    scene.screenplayFormats = screenplayFormats;
    ScreenplayLayoutService layout;
    return layout.layoutScene(scene, paper).durationMinutes;
}

double ScriptService::estimatedMinutes(const Project& project) {
    return estimatedMinutes(project, ScreenplayPaper::HollywoodLetter);
}

double ScriptService::estimatedMinutes(const Project& project, ScreenplayPaper paper) {
    ScreenplayLayoutService layout;
    return layout.layout(project, paper, false).durationMinutes;
}

}  // namespace mezozoy
