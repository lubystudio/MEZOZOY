#include "ScreenplayDocumentModel.h"

#include "../core/Utf.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace mezozoy {
namespace {

std::wstring NormalizeNewlines(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == L'\r') {
            if (index + 1 < value.size() && value[index + 1] == L'\n') ++index;
            result.push_back(L'\n');
        } else {
            result.push_back(value[index]);
        }
    }
    return result;
}

std::vector<std::wstring> SceneLines(const Scene& scene) {
    std::wstring text = NormalizeNewlines(scene.text);
    if (utf::Trim(text).empty()) text = scene.title;
    std::vector<std::wstring> lines = utf::SplitLines(text);
    if (lines.empty()) lines.push_back(scene.title);
    if (utf::Trim(lines.front()) != utf::Trim(scene.title)) lines.insert(lines.begin(), scene.title);
    return lines;
}

std::wstring Fingerprint(const std::vector<ScreenplayBlock>& blocks, const ScreenplaySceneSection& section,
                         bool includeHeading) {
    const std::size_t begin = section.firstBlock + (includeHeading ? 0 : 1);
    const std::size_t end = std::min(blocks.size(), section.firstBlock + section.blockCount);
    std::wstring result;
    for (std::size_t index = begin; index < end; ++index) {
        const std::wstring value = utf::ToUpper(utf::Trim(blocks[index].text));
        if (value.empty()) continue;
        if (!result.empty()) result.push_back(L'\n');
        result += value;
    }
    return result;
}

std::wstring SceneBodyFingerprint(const Scene& scene) {
    std::vector<std::wstring> lines = SceneLines(scene);
    std::wstring result;
    for (std::size_t index = 1; index < lines.size(); ++index) {
        const std::wstring value = utf::ToUpper(utf::Trim(lines[index]));
        if (value.empty()) continue;
        if (!result.empty()) result.push_back(L'\n');
        result += value;
    }
    return result;
}

std::wstring SummaryFromBlocks(const std::vector<ScreenplayBlock>& blocks,
                               const ScreenplaySceneSection& section) {
    const std::size_t end = std::min(blocks.size(), section.firstBlock + section.blockCount);
    for (std::size_t index = section.firstBlock + 1; index < end; ++index) {
        if (blocks[index].type != ScriptLineType::Action) continue;
        const std::wstring value = utf::Trim(blocks[index].text);
        if (!value.empty()) return value;
    }
    return {};
}

}  // namespace

ScreenplayDocumentModel ScreenplayDocumentModel::fromProject(const Project& project) {
    ScreenplayDocumentModel model;
    ScriptService scripts;
    for (std::size_t sceneIndex = 0; sceneIndex < project.scenes.size(); ++sceneIndex) {
        const Scene& scene = project.scenes[sceneIndex];
        const std::vector<std::wstring> lines = SceneLines(scene);
        std::wstring formats = scene.screenplayFormats;
        const std::wstring sceneText = utf::JoinLines(lines);
        const std::wstring fallback = scripts.buildFormatMap(sceneText);
        const auto originalLines = utf::SplitLines(NormalizeNewlines(scene.text));
        if (!originalLines.empty() && utf::Trim(originalLines.front()) != utf::Trim(scene.title) &&
            !utf::Trim(scene.text).empty()) formats.insert(formats.begin(), L'H');
        for (std::size_t line = formats.size(); line < lines.size(); ++line) formats.push_back(fallback[line]);
        formats.resize(lines.size());

        ScreenplaySceneSection section;
        section.sourceSceneId = scene.id;
        section.firstBlock = model.blocks_.size();
        for (std::size_t line = 0; line < lines.size(); ++line) {
            ScriptLineType type = line == 0 ? ScriptLineType::SceneHeading
                                            : ScriptService::typeFromFormatCode(formats[line]);
            if (type == ScriptLineType::Empty && !utf::Trim(lines[line]).empty())
                type = scripts.classify(lines[line]);
            model.blocks_.push_back({type, lines[line]});
        }
        section.blockCount = model.blocks_.size() - section.firstBlock;
        model.sections_.push_back(section);
    }
    return model;
}

ScreenplayDocumentModel ScreenplayDocumentModel::parse(const std::wstring& text,
                                                        const std::wstring& formatMap,
                                                        const Project& existingProject,
                                                        std::wstring* error) {
    ScreenplayDocumentModel model;
    ScriptService scripts;
    const std::vector<std::wstring> lines = utf::SplitLines(NormalizeNewlines(text));
    if (lines.empty()) {
        if (error) *error = L"Сценарий не содержит текста.";
        return model;
    }

    ScriptLineType previous = ScriptLineType::Empty;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        ScriptLineType type = index < formatMap.size()
            ? ScriptService::typeFromFormatCode(formatMap[index]) : ScriptLineType::Empty;
        if (type == ScriptLineType::Empty && !utf::Trim(lines[index]).empty())
            type = index == 0 ? ScriptLineType::SceneHeading : scripts.classify(lines[index], previous);
        model.blocks_.push_back({type, lines[index]});
        previous = type;
    }

    std::size_t firstHeading = model.blocks_.size();
    for (std::size_t index = 0; index < model.blocks_.size(); ++index) {
        if (model.blocks_[index].type == ScriptLineType::SceneHeading &&
            !utf::Trim(model.blocks_[index].text).empty()) {
            firstHeading = index;
            break;
        }
    }
    if (firstHeading == model.blocks_.size()) {
        firstHeading = 0;
        while (firstHeading + 1 < model.blocks_.size() && utf::Trim(model.blocks_[firstHeading].text).empty())
            ++firstHeading;
        model.blocks_[firstHeading].type = ScriptLineType::SceneHeading;
        if (utf::Trim(model.blocks_[firstHeading].text).empty())
            model.blocks_[firstHeading].text = L"НОВАЯ СЦЕНА";
    }
    if (firstHeading > 0) model.blocks_.erase(model.blocks_.begin(), model.blocks_.begin() + firstHeading);

    for (std::size_t index = 0; index < model.blocks_.size(); ++index) {
        const bool heading = model.blocks_[index].type == ScriptLineType::SceneHeading &&
                             !utf::Trim(model.blocks_[index].text).empty();
        if (!heading && !model.sections_.empty()) continue;
        if (!heading) model.blocks_[index].type = ScriptLineType::SceneHeading;
        if (!model.sections_.empty())
            model.sections_.back().blockCount = index - model.sections_.back().firstBlock;
        model.sections_.push_back({0, index, 0});
    }
    if (!model.sections_.empty())
        model.sections_.back().blockCount = model.blocks_.size() - model.sections_.back().firstBlock;
    if (model.sections_.empty()) {
        if (error) *error = L"Не удалось определить первую сцену сценария.";
        return {};
    }

    model.matchExistingScenes(existingProject);
    if (error) error->clear();
    return model;
}

void ScreenplayDocumentModel::matchExistingScenes(const Project& project) {
    std::vector<int> matches(sections_.size(), -1);
    std::vector<bool> used(project.scenes.size(), false);
    std::vector<std::wstring> oldBodies;
    oldBodies.reserve(project.scenes.size());
    for (const Scene& scene : project.scenes) oldBodies.push_back(SceneBodyFingerprint(scene));

    for (std::size_t sectionIndex = 0; sectionIndex < sections_.size(); ++sectionIndex) {
        const std::wstring body = Fingerprint(blocks_, sections_[sectionIndex], false);
        if (body.empty()) continue;
        for (std::size_t oldIndex = 0; oldIndex < project.scenes.size(); ++oldIndex) {
            if (!used[oldIndex] && body == oldBodies[oldIndex]) {
                matches[sectionIndex] = static_cast<int>(oldIndex);
                used[oldIndex] = true;
                break;
            }
        }
    }

    for (std::size_t sectionIndex = 0; sectionIndex < sections_.size(); ++sectionIndex) {
        if (matches[sectionIndex] >= 0) continue;
        const std::wstring title = Fingerprint(blocks_, sections_[sectionIndex], true);
        const std::size_t newline = title.find(L'\n');
        const std::wstring heading = newline == std::wstring::npos ? title : title.substr(0, newline);
        for (std::size_t oldIndex = 0; oldIndex < project.scenes.size(); ++oldIndex) {
            if (!used[oldIndex] && heading == utf::ToUpper(utf::Trim(project.scenes[oldIndex].title))) {
                matches[sectionIndex] = static_cast<int>(oldIndex);
                used[oldIndex] = true;
                break;
            }
        }
    }

    if (sections_.size() == project.scenes.size()) {
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            if (matches[index] < 0 && !used[index]) {
                matches[index] = static_cast<int>(index);
                used[index] = true;
            }
        }
    }
    for (std::size_t index = 0; index < sections_.size(); ++index) {
        if (matches[index] >= 0)
            sections_[index].sourceSceneId = project.scenes[static_cast<std::size_t>(matches[index])].id;
    }
}

std::wstring ScreenplayDocumentModel::text() const {
    std::vector<std::wstring> lines;
    lines.reserve(blocks_.size());
    for (const ScreenplayBlock& block : blocks_) lines.push_back(block.text);
    return utf::JoinLines(lines);
}

std::wstring ScreenplayDocumentModel::formatMap() const {
    std::wstring result;
    result.reserve(blocks_.size());
    for (const ScreenplayBlock& block : blocks_) result.push_back(ScriptService::formatCode(block.type));
    return result;
}

bool ScreenplayDocumentModel::apply(Project& project, std::wstring* error) const {
    if (sections_.empty()) {
        if (error) *error = L"В документе не найдено ни одной сцены.";
        return false;
    }

    std::unordered_map<int, const Scene*> existing;
    for (const Scene& scene : project.scenes) existing.emplace(scene.id, &scene);
    int nextId = project.nextId();
    std::vector<Scene> scenes;
    scenes.reserve(sections_.size());
    for (std::size_t sectionIndex = 0; sectionIndex < sections_.size(); ++sectionIndex) {
        const ScreenplaySceneSection& section = sections_[sectionIndex];
        Scene scene;
        const auto source = existing.find(section.sourceSceneId);
        if (source != existing.end()) scene = *source->second;
        else {
            scene.id = nextId++;
            if (!scenes.empty()) scene.documentId = scenes.back().documentId;
            else if (!project.documents.empty()) scene.documentId = project.documents.front().id;
        }

        std::size_t end = std::min(blocks_.size(), section.firstBlock + section.blockCount);
        std::vector<std::wstring> lines;
        std::wstring formats;
        for (std::size_t index = section.firstBlock; index < end; ++index) {
            lines.push_back(blocks_[index].text);
            formats.push_back(ScriptService::formatCode(index == section.firstBlock
                ? ScriptLineType::SceneHeading : blocks_[index].type));
        }
        if (lines.empty()) lines.push_back(L"НОВАЯ СЦЕНА");
        std::wstring title = utf::Trim(lines.front());
        if (title.empty()) title = L"НОВАЯ СЦЕНА";
        if (formats.empty()) formats.push_back(ScriptService::formatCode(ScriptLineType::SceneHeading));

        const std::wstring newText = utf::JoinLines(lines);
        if (scene.text != newText || scene.screenplayFormats != formats) scene.rtf.clear();
        scene.title = title;
        scene.heading = title;
        scene.text = newText;
        scene.screenplayFormats = formats;
        scene.titleAutoGenerated = false;
        if (!scene.summaryManual) scene.summary = SummaryFromBlocks(blocks_, section);
        scene.orderIndex = static_cast<int>(sectionIndex);
        for (BreakdownItem& item : scene.breakdownItems) item.sceneId = scene.id;
        for (ReviewComment& comment : scene.reviewComments) comment.sceneId = scene.id;
        scenes.push_back(std::move(scene));
    }

    project.scenes = std::move(scenes);
    project.normalizeOrder();
    if (error) error->clear();
    return true;
}

}  // namespace mezozoy
