#pragma once

#include "../core/Models.h"

#include <string>
#include <vector>

namespace mezozoy {

struct SceneStatistics {
    int number = 0;
    std::wstring title;
    std::size_t words = 0;
    double minutes = 0.0;
    std::wstring characters;
    int dialogueLines = 0;
    int actionLines = 0;
    int reviewComments = 0;
    int breakdownItems = 0;
};

struct CharacterStatistics {
    std::wstring name;
    int dialogueLines = 0;
    std::size_t spokenWords = 0;
    int scenes = 0;
    int firstScene = 0;
    int lastScene = 0;
};

struct LocationStatistics {
    std::wstring name;
    int scenes = 0;
    std::size_t words = 0;
    double minutes = 0.0;
};

struct StatisticsSnapshot {
    std::size_t totalWords = 0;
    std::size_t totalCharacters = 0;
    std::size_t totalLines = 0;
    double pages = 0.0;
    double minutes = 0.0;
    std::size_t emptyScenes = 0;
    std::size_t todoCount = 0;
    std::size_t openReviewComments = 0;
    std::size_t breakdownItems = 0;
    std::size_t dialogueLines = 0;
    std::size_t actionLines = 0;
    std::vector<SceneStatistics> scenes;
    std::vector<CharacterStatistics> characters;
    std::vector<LocationStatistics> locations;
};

class StatisticsService {
public:
    StatisticsSnapshot analyze(const Project& project) const;
};

}  // namespace mezozoy
