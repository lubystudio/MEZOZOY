#pragma once

#include "ScriptService.h"
#include "../core/Models.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mezozoy {

struct ScreenplayBlock {
    ScriptLineType type = ScriptLineType::Action;
    std::wstring text;
};

struct ScreenplaySceneSection {
    int sourceSceneId = 0;
    std::size_t firstBlock = 0;
    std::size_t blockCount = 0;
};

class ScreenplayDocumentModel {
public:
    static ScreenplayDocumentModel fromProject(const Project& project);
    static ScreenplayDocumentModel parse(const std::wstring& text, const std::wstring& formatMap,
                                         const Project& existingProject, std::wstring* error = nullptr);

    bool empty() const { return sections_.empty(); }
    const std::vector<ScreenplayBlock>& blocks() const { return blocks_; }
    const std::vector<ScreenplaySceneSection>& sections() const { return sections_; }
    std::wstring text() const;
    std::wstring formatMap() const;
    bool apply(Project& project, std::wstring* error = nullptr) const;

private:
    std::vector<ScreenplayBlock> blocks_;
    std::vector<ScreenplaySceneSection> sections_;

    void matchExistingScenes(const Project& project);
};

}  // namespace mezozoy
