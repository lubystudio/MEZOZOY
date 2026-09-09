#pragma once

#include "../core/Models.h"

#include <string>
#include <vector>

namespace mezozoy {

enum class SearchResultKind { Scene, Character, Location, World, Reference, Breakdown, Review, Document };

struct SearchResult {
    SearchResultKind kind = SearchResultKind::Scene;
    int itemId = 0;
    int sceneId = 0;
    std::wstring category;
    std::wstring title;
    std::wstring preview;
};

class GlobalSearchService {
public:
    std::vector<SearchResult> search(const Project& project, const std::wstring& query, std::size_t limit = 250) const;
};

}  // namespace mezozoy
