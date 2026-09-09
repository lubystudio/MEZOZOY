#pragma once

#include "../core/Models.h"

#include <string>

namespace mezozoy {

class BreakdownService {
public:
    BreakdownItem* add(Project& project, int sceneId, BreakdownCategory category, const std::wstring& name,
                       const std::wstring& selectedText = {}) const;
    bool remove(Project& project, int sceneId, int itemId) const;
    std::wstring report(const Project& project) const;
};

}  // namespace mezozoy
