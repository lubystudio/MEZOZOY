#pragma once

#include "../core/Models.h"

#include <string>

namespace mezozoy {

class ReviewService {
public:
    ReviewComment* add(Project& project, int sceneId, ReviewMarkType type, int startIndex, int length,
                       const std::wstring& selectedText, const std::wstring& comment, const std::wstring& author) const;
    bool remove(Project& project, int sceneId, int commentId) const;
    bool setResolved(Project& project, int sceneId, int commentId, bool resolved) const;
    std::wstring report(const Project& project, bool includeResolved) const;
};

}  // namespace mezozoy
