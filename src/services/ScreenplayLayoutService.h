#pragma once

#include "ScriptService.h"
#include "../core/Models.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mezozoy {

enum class ScreenplayPaper {
    HollywoodLetter,
    A4
};

struct ScreenplayPageSpec {
    ScreenplayPaper paper = ScreenplayPaper::HollywoodLetter;
    std::wstring name;
    double widthInches = 8.5;
    double heightInches = 11.0;
    double marginLeftInches = 1.5;
    double marginRightInches = 1.0;
    double marginTopInches = 1.0;
    double marginBottomInches = 1.0;
    double fontPoints = 12.0;
    double lineHeightInches = 1.0 / 6.0;
    int bodyLineCapacity = 54;
};

struct ScreenplayRenderLine {
    std::wstring text;
    ScriptLineType type = ScriptLineType::Action;
    int sceneId = 0;
    std::size_t sceneIndex = 0;
    std::size_t paragraphIndex = 0;
    std::size_t wrappedLineIndex = 0;
    // UTF-16 positions in the continuous document, with one character per newline.
    std::size_t sourceStart = 0;
    std::size_t sourceEnd = 0;
    int slot = 0;
};

struct ScreenplayLayoutPage {
    bool titlePage = false;
    int scriptPageNumber = 0;
    int usedSlots = 0;
    int startSceneId = 0;
    std::size_t startSceneIndex = 0;
    std::size_t startParagraphIndex = 0;
    std::vector<ScreenplayRenderLine> lines;
};

struct ScreenplayLayoutDocument {
    ScreenplayPaper paper = ScreenplayPaper::HollywoodLetter;
    ScreenplayPageSpec spec;
    std::vector<ScreenplayLayoutPage> pages;
    std::size_t scriptPageCount = 0;
    std::size_t usedLineSlots = 0;
    double filledScriptPages = 0.0;
    double durationMinutes = 0.0;
};

struct ScreenplayElementMetrics {
    double leftInches = 1.5;
    double widthInches = 6.0;
    bool bold = false;
    bool italic = false;
    bool centered = false;
    bool rightAligned = false;
};

class ScreenplayLayoutService {
public:
    ScreenplayLayoutDocument layout(const Project& project, ScreenplayPaper paper,
                                    bool includeTitlePage = true) const;
    ScreenplayLayoutDocument layoutScene(const Scene& scene, ScreenplayPaper paper) const;
    ScreenplayLayoutDocument layoutText(const std::wstring& text, const std::wstring& formats,
                                       ScreenplayPaper paper, bool includeTitlePage = false) const;

    static ScreenplayPageSpec pageSpec(ScreenplayPaper paper);
    static ScreenplayPaper paperFromSetting(const std::wstring& value);
    static std::wstring paperSetting(ScreenplayPaper paper);
    static std::wstring paperLabel(ScreenplayPaper paper);
    static ScreenplayElementMetrics elementMetrics(ScriptLineType type, const ScreenplayPageSpec& spec);
};

}  // namespace mezozoy
