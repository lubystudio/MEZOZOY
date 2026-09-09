#include "ScreenplayLayoutService.h"
#include "ScreenplayDocumentModel.h"

#include "../core/Utf.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace mezozoy {
namespace {

struct Paragraph {
    std::wstring text;
    ScriptLineType type = ScriptLineType::Action;
    int sceneId = 0;
    std::size_t sceneIndex = 0;
    std::size_t paragraphIndex = 0;
    int leadingSlots = 0;
    std::vector<std::wstring> wrapped;
    std::vector<std::size_t> wrapStarts;
    std::size_t sourceStart = 0;
};

void Wrap(Paragraph& paragraph, std::size_t columns) {
    const std::wstring& text = paragraph.text;
    columns = std::max<std::size_t>(8, columns);
    std::size_t start = 0;
    do {
        std::size_t end = std::min(text.size(), start + columns);
        if (end < text.size() && end > start && !std::iswspace(text[end])) {
            const std::size_t space = text.find_last_of(L" \t", end - 1);
            if (space != std::wstring::npos && space >= start) end = space + 1;
        }
        // Inter-word whitespace belongs to the previous visual row, not to
        // a spurious indent at the start of the continuation.
        while (end < text.size() && std::iswspace(text[end])) ++end;
        // A surrogate pair must never straddle two visual lines.
        if (end < text.size() && end > start && text[end - 1] >= 0xd800 && text[end - 1] <= 0xdbff) --end;
        paragraph.wrapStarts.push_back(start);
        paragraph.wrapped.push_back(text.substr(start, end - start));
        start = end;
    } while (start < text.size());
}

std::wstring NormalizeNewlines(const std::wstring& text) {
    std::wstring result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            result += L'\n';
        } else result += text[i];
    }
    return result;
}

std::size_t ColumnsFor(ScriptLineType type, const ScreenplayPageSpec& spec) {
    const ScreenplayElementMetrics metrics = ScreenplayLayoutService::elementMetrics(type, spec);
    return static_cast<std::size_t>(std::max(8.0, std::floor(metrics.widthInches * 10.0)));
}

int LeadingSlots(ScriptLineType type, ScriptLineType previous, bool firstParagraph) {
    if (firstParagraph || previous == ScriptLineType::Empty) return 0;
    switch (type) {
        case ScriptLineType::SceneHeading:
        case ScriptLineType::Character:
        case ScriptLineType::Transition:
        case ScriptLineType::Action:
            return 1;
        default:
            return 0;
    }
}

std::vector<Paragraph> BuildParagraphs(const std::wstring& text, const std::wstring& formats,
                                      const ScreenplayPageSpec& spec) {
    std::vector<Paragraph> paragraphs;
    ScriptService scripts;
    ScriptLineType previous = ScriptLineType::Empty;
    const auto lines = utf::SplitLines(NormalizeNewlines(text));
    std::size_t source = 0, sceneIndex = 0, sceneParagraph = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        ScriptLineType type = index < formats.size()
            ? ScriptService::typeFromFormatCode(formats[index]) : scripts.classify(lines[index], previous);
        if (index == 0 && !utf::Trim(lines[index]).empty()) type = ScriptLineType::SceneHeading;
        if (type == ScriptLineType::SceneHeading && index > 0) { ++sceneIndex; sceneParagraph = 0; }
        Paragraph paragraph;
        paragraph.text = lines[index];
        paragraph.type = type;
        paragraph.sceneId = static_cast<int>(sceneIndex + 1);
        paragraph.sceneIndex = sceneIndex;
        paragraph.paragraphIndex = sceneParagraph++;
        paragraph.sourceStart = source;
        paragraph.leadingSlots = utf::Trim(lines[index]).empty() ? 0
            : LeadingSlots(type, previous, paragraphs.empty());
        Wrap(paragraph, ColumnsFor(type, spec));
        paragraphs.push_back(std::move(paragraph));
        source += lines[index].size() + 1;
        previous = utf::Trim(lines[index]).empty() ? ScriptLineType::Empty : type;
    }
    return paragraphs;
}

void StartScriptPage(ScreenplayLayoutDocument& document) {
    ScreenplayLayoutPage page;
    page.scriptPageNumber = static_cast<int>(document.scriptPageCount + 1);
    document.pages.push_back(std::move(page));
    ++document.scriptPageCount;
}

ScreenplayLayoutPage& CurrentScriptPage(ScreenplayLayoutDocument& document) {
    return document.pages.back();
}

}  // namespace

ScreenplayPageSpec ScreenplayLayoutService::pageSpec(ScreenplayPaper paper) {
    ScreenplayPageSpec result;
    result.paper = paper;
    if (paper == ScreenplayPaper::A4) {
        result.name = L"A4";
        result.widthInches = 8.2677165354;
        result.heightInches = 11.692913386;
    } else {
        result.name = L"Hollywood / US Letter";
        result.widthInches = 8.5;
        result.heightInches = 11.0;
    }
    result.bodyLineCapacity = std::max(1, static_cast<int>(std::floor(
        (result.heightInches - result.marginTopInches - result.marginBottomInches) /
        result.lineHeightInches + 0.0001)));
    return result;
}

ScreenplayPaper ScreenplayLayoutService::paperFromSetting(const std::wstring& value) {
    const std::wstring upper = utf::ToUpper(utf::Trim(value));
    return upper == L"A4" ? ScreenplayPaper::A4 : ScreenplayPaper::HollywoodLetter;
}

std::wstring ScreenplayLayoutService::paperSetting(ScreenplayPaper paper) {
    return paper == ScreenplayPaper::A4 ? L"A4" : L"HollywoodLetter";
}

std::wstring ScreenplayLayoutService::paperLabel(ScreenplayPaper paper) {
    return paper == ScreenplayPaper::A4 ? L"A4" : L"Hollywood / US Letter";
}

ScreenplayElementMetrics ScreenplayLayoutService::elementMetrics(ScriptLineType type,
                                                                  const ScreenplayPageSpec& spec) {
    ScreenplayElementMetrics result;
    result.leftInches = spec.marginLeftInches;
    result.widthInches = spec.widthInches - spec.marginLeftInches - spec.marginRightInches;
    switch (type) {
        case ScriptLineType::SceneHeading:
            result.bold = true;
            break;
        case ScriptLineType::Character:
            result.leftInches = 3.7;
            result.widthInches = std::min(2.2, spec.widthInches - spec.marginRightInches - result.leftInches);
            result.bold = true;
            break;
        case ScriptLineType::Parenthetical:
            result.leftInches = 3.1;
            result.widthInches = std::min(2.5, spec.widthInches - spec.marginRightInches - result.leftInches);
            result.italic = true;
            break;
        case ScriptLineType::Dialogue:
            result.leftInches = 2.5;
            result.widthInches = std::min(3.5, spec.widthInches - spec.marginRightInches - result.leftInches);
            break;
        case ScriptLineType::Transition:
            result.widthInches = 2.0;
            result.leftInches = spec.widthInches - spec.marginRightInches - result.widthInches;
            result.rightAligned = true;
            break;
        case ScriptLineType::Note:
            result.italic = true;
            break;
        case ScriptLineType::Lyrics:
            result.leftInches = 2.5;
            result.widthInches = std::min(3.5, spec.widthInches - spec.marginRightInches - result.leftInches);
            result.italic = true;
            result.centered = true;
            break;
        default:
            break;
    }
    result.widthInches = std::max(0.75, result.widthInches);
    return result;
}

ScreenplayLayoutDocument ScreenplayLayoutService::layout(const Project& project, ScreenplayPaper paper,
                                                           bool includeTitlePage) const {
    const auto model = ScreenplayDocumentModel::fromProject(project);
    auto result = layoutText(model.text(), model.formatMap(), paper, includeTitlePage);
    for (auto& page : result.pages) {
        if (page.startSceneIndex < project.scenes.size()) page.startSceneId = project.scenes[page.startSceneIndex].id;
        for (auto& line : page.lines)
            if (line.sceneIndex < project.scenes.size()) line.sceneId = project.scenes[line.sceneIndex].id;
    }
    return result;
}

ScreenplayLayoutDocument ScreenplayLayoutService::layoutText(const std::wstring& text, const std::wstring& formats,
                                                             ScreenplayPaper paper, bool includeTitlePage) const {
    ScreenplayLayoutDocument document;
    document.paper = paper;
    document.spec = pageSpec(paper);
    if (includeTitlePage) {
        ScreenplayLayoutPage title;
        title.titlePage = true;
        document.pages.push_back(std::move(title));
    }
    StartScriptPage(document);

    const std::vector<Paragraph> paragraphs = BuildParagraphs(text, formats, document.spec);
    const int capacity = document.spec.bodyLineCapacity;
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        const Paragraph& paragraph = paragraphs[index];
        ScreenplayLayoutPage* page = &CurrentScriptPage(document);
        int remaining = capacity - page->usedSlots;
        const int paragraphSlots = paragraph.leadingSlots + static_cast<int>(paragraph.wrapped.size());

        int keepSlots = paragraph.leadingSlots + std::min(2, static_cast<int>(paragraph.wrapped.size()));
        if (paragraph.type == ScriptLineType::SceneHeading) {
            keepSlots = paragraphSlots;
            for (std::size_t look = index + 1; look < paragraphs.size(); ++look) {
                if (paragraphs[look].sceneId != paragraph.sceneId) break;
                keepSlots += paragraphs[look].leadingSlots + std::min(2, static_cast<int>(paragraphs[look].wrapped.size()));
                if (!utf::Trim(paragraphs[look].text).empty()) break;
            }
        }
        if (paragraph.type == ScriptLineType::Character) {
            keepSlots = paragraphSlots;
            for (std::size_t look = index + 1; look < paragraphs.size() && look <= index + 2; ++look) {
                if (paragraphs[look].sceneId != paragraph.sceneId) break;
                if (paragraphs[look].type != ScriptLineType::Parenthetical &&
                    paragraphs[look].type != ScriptLineType::Dialogue) break;
                keepSlots += paragraphs[look].leadingSlots + static_cast<int>(paragraphs[look].wrapped.size());
            }
            keepSlots = std::min(6, keepSlots);
        }

        if (page->usedSlots > 0 && remaining < std::min(capacity, keepSlots)) {
            StartScriptPage(document);
            page = &CurrentScriptPage(document);
            remaining = capacity;
        }

        if (paragraph.leadingSlots > 0 && page->usedSlots > 0) {
            const int availableLeading = std::min(paragraph.leadingSlots, capacity - page->usedSlots);
            page->usedSlots += availableLeading;
            if (page->usedSlots >= capacity && !paragraph.wrapped.empty()) {
                StartScriptPage(document);
                page = &CurrentScriptPage(document);
            }
        }

        for (std::size_t wrappedIndex = 0; wrappedIndex < paragraph.wrapped.size(); ++wrappedIndex) {
            if (page->usedSlots >= capacity) {
                StartScriptPage(document);
                page = &CurrentScriptPage(document);
            }
            if (page->lines.empty()) {
                page->startSceneId = paragraph.sceneId;
                page->startSceneIndex = paragraph.sceneIndex;
                page->startParagraphIndex = paragraph.paragraphIndex;
            }
            ScreenplayRenderLine line;
            line.text = paragraph.wrapped[wrappedIndex];
            line.type = paragraph.type;
            line.sceneId = paragraph.sceneId;
            line.sceneIndex = paragraph.sceneIndex;
            line.paragraphIndex = paragraph.paragraphIndex;
            line.wrappedLineIndex = wrappedIndex;
            line.sourceStart = paragraph.sourceStart + paragraph.wrapStarts[wrappedIndex];
            line.sourceEnd = line.sourceStart + line.text.size();
            line.slot = page->usedSlots;
            page->lines.push_back(std::move(line));
            ++page->usedSlots;
        }
    }

    for (const ScreenplayLayoutPage& page : document.pages) {
        if (page.titlePage) continue;
        document.usedLineSlots += static_cast<std::size_t>(page.usedSlots);
    }
    if (document.scriptPageCount > 0) {
        const ScreenplayLayoutPage& last = document.pages.back();
        const double lastFraction = last.usedSlots > 0
            ? static_cast<double>(last.usedSlots) / static_cast<double>(capacity)
            : 0.0;
        document.filledScriptPages = document.scriptPageCount > 1
            ? static_cast<double>(document.scriptPageCount - 1) + lastFraction
            : lastFraction;
    }
    document.durationMinutes = document.filledScriptPages;
    return document;
}

ScreenplayLayoutDocument ScreenplayLayoutService::layoutScene(const Scene& scene, ScreenplayPaper paper) const {
    Project project;
    project.title = scene.title;
    project.scenes = {scene};
    return layout(project, paper, false);
}

}  // namespace mezozoy
