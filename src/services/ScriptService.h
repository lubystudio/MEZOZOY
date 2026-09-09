#pragma once

#include "../core/Models.h"

#include <string>
#include <vector>

namespace mezozoy {

enum class ScreenplayPaper;

enum class ScriptLineType {
    Empty,
    SceneHeading,
    Participants,
    Action,
    Character,
    Parenthetical,
    Dialogue,
    Transition,
    Note,
    Shot,
    Lyrics
};

struct SceneRange {
    std::size_t sceneIndex = 0;
    std::size_t start = 0;
    std::size_t length = 0;
};

class ScriptService {
public:
    std::wstring buildFullScript(const Project& project) const;
    std::vector<SceneRange> calculateRanges(const Project& project, const std::wstring& fullText) const;
    void applyFullScript(Project& project, const std::wstring& fullText) const;
    bool renameScene(Project& project, std::size_t sceneIndex, const std::wstring& title) const;
    ScriptLineType classify(const std::wstring& line, ScriptLineType previous = ScriptLineType::Empty) const;
    std::wstring buildFormatMap(const std::wstring& sceneText) const;
    static wchar_t formatCode(ScriptLineType type);
    static ScriptLineType typeFromFormatCode(wchar_t code);
    static std::wstring typeName(ScriptLineType type);
    static ScriptLineType nextTypeAfterEnter(ScriptLineType type);
    static ScriptLineType nextTypeAfterTab(ScriptLineType type);
    static ScriptLineType typeForEmptyEnter(ScriptLineType type);
    static std::size_t countWords(const std::wstring& text);
    static std::size_t estimatedPrintLines(const std::wstring& text, const std::wstring& screenplayFormats = {});
    static std::size_t estimatedPrintLines(const std::wstring& text, const std::wstring& screenplayFormats,
                                           ScreenplayPaper paper);
    static double estimatedMinutes(const std::wstring& text, const std::wstring& screenplayFormats = {});
    static double estimatedMinutes(const std::wstring& text, const std::wstring& screenplayFormats,
                                   ScreenplayPaper paper);
    static double estimatedMinutes(const Project& project);
    static double estimatedMinutes(const Project& project, ScreenplayPaper paper);

private:
    static std::wstring normalizeNewlines(const std::wstring& value);
    static std::size_t findTitleLine(const std::wstring& text, const std::wstring& title, std::size_t from);
};

}  // namespace mezozoy
